#!/usr/bin/env python3
"""Run actual CPU and Vulkan workloads; fail on crashes, violated limits, or invalid output."""
import argparse
import datetime as dt
import json
import pathlib
import platform
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, default=pathlib.Path('build'))
    parser.add_argument('--frames', type=int, default=600)
    parser.add_argument('--profile', choices=('rtx3080-4k', 'rx9060xt-1440p'), help='Target PC resolution; runs here on the selected local Vulkan device')
    parser.add_argument('--render-frames', type=int, default=60, help='Rendered frames per scenario (first five warm up)')
    parser.add_argument('--full', action='store_true', help='Also probe 1080p, 1440p and 4K; not target-FPS certification')
    parser.add_argument('--cpu-only', action='store_true')
    args = parser.parse_args()
    if args.full and args.profile:
        parser.error('Choose --full or --profile')
    if args.render_frames < 12 or args.render_frames > 100000:
        parser.error('--render-frames must be 12..100000')
    profile = None
    if args.profile:
        profiles_path = pathlib.Path(__file__).resolve().parent.parent / 'config' / 'hardware_profiles.json'
        profile = json.loads(profiles_path.read_text())[args.profile]
    if args.frames < 60:
        parser.error('--frames must be at least 60 to exercise teleport and edit cycles')
    suffix = '.exe' if sys.platform == 'win32' else ''
    build = args.build.resolve()
    output = pathlib.Path('artifacts') / dt.datetime.now(dt.timezone.utc).strftime('stress-%Y%m%dT%H%M%S%fZ')
    output.mkdir(parents=True)
    results = []

    def run(exe, extra, name):
        command = [str(build / (exe + suffix)), *map(str, extra)]
        print('Running', name, flush=True)
        with (output / (name + '.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
        return output / name

    run('vc_tests', [], 'core')
    for side in (16, 32):
        for scenario in ('stationary', 'flight', 'teleport', 'edits'):
            name = f'cpu-{scenario}-{side}'
            prefix = output / name
            run('vc_bench', ['--frames', args.frames, '--scenario', scenario, '--side', side,
                             '--budget-mib', profile['stress_cache_mib'] if profile else 1, '--output', prefix], name)
            data = json.loads(prefix.with_suffix('.json').read_text())
            assert data['completed'] == data['submitted'] and data['completed'] > 0, data
            assert data['peak_cache_bytes'] <= data['cache_budget_bytes'], data
            assert data['settled_resident_bytes'] > 0, data
            assert data['main_thread_ms']['p99'] >= 0, data
            results.append(data)
    if not args.cpu_only:
        resolutions = [(640, 360)] + ([(1920, 1080), (2560, 1440), (3840, 2160)] if args.full else [])
        if profile:
            resolutions = [(profile['render_width'], profile['render_height'])]
        for width, height in resolutions:
            for scenario in ('stationary', 'flight', 'yaw', 'edits'):
                name = f'vk-{scenario}-{width}x{height}'
                prefix = output / name
                ppm = prefix.with_suffix('.ppm')
                run('vibecraft', ['--headless', '--validation', '--frames', args.render_frames,
                                  '--width', width, '--height', height, '--radius', profile['near_radius_chunks'] if profile else 3,
                                  '--scenario', scenario, '--world', output / 'stress-world.vced',
                                  '--screenshot', ppm, '--output', prefix], name)
                data = json.loads(prefix.with_suffix('.json').read_text())
                assert data['validation_errors'] == 0 and data['frames'] == args.render_frames, data
                assert data['triangles'] > 100 and data['gpu_opaque_ms']['median'] > 0, data
                # Check actual readback: dimensions, sky and terrain pixels, and nonuniform geometry.
                with ppm.open('rb') as image:
                    assert image.readline() == b'P6\n'
                    assert image.readline().strip() == f'{width} {height}'.encode()
                    assert image.readline() == b'255\n'
                    pixels = image.read()
                assert len(pixels) == width * height * 3
                colors = set(zip(pixels[0::3], pixels[1::3], pixels[2::3]))
                assert len(colors) > 8, f'{name}: uniform/empty readback'
                assert any(g > r * 1.3 and g > b for r, g, b in colors), f'{name}: no terrain'
                assert any(b > g > r for r, g, b in colors), f'{name}: no sky'
                results.append(data)
    summary = {'platform': platform.platform(), 'python': platform.python_version(),
               'note': 'CPU streaming and software/hardware Vulkan experiments; no target-FPS claim.',
               'requested_hardware_profile': profile, 'results': results}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'PASS: {len(results)} workloads plus core tests. Evidence: {output}')


if __name__ == '__main__':
    try:
        main()
    except (subprocess.SubprocessError, AssertionError, OSError, ValueError) as error:
        print(f'Stress suite FAILED: {error}', file=sys.stderr)
        sys.exit(1)
