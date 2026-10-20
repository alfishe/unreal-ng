#!/usr/bin/env python3
"""Every clip frame (clips.py) through every preset and unreal-qt's profiles, then a VP9 4:4:4 webm per clip and shader
into <work>/video/<clip>/<key>.webm (ffmpeg). librashader-cli renders one frame per call: no frame history, so
presets with feedback or history passes show each frame on its own.

usage: render_clips.py [--work DIR] [--slang DIR] [--cli PATH] [--render PATH] [--jobs N] [key ...]"""
import glob
import os
import subprocess
from concurrent.futures import ThreadPoolExecutor

from common import librashader, ours, parser
from presets import OH, OURS, OW, PRESETS


def main():
    p = parser(__doc__, cli=True, render=True)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('keys', nargs='*')
    args = p.parse_args()
    only = set(args.keys)
    video = os.path.join(args.work, 'video')
    jobs = []
    for clip in sorted(os.listdir(os.path.join(args.work, 'clips'))):
        frames = sorted(glob.glob(os.path.join(args.work, 'clips', clip, '*.png')))
        for key, preset in [(f'ours-{k}', None) for k in OURS] + PRESETS:
            if not only or key in only:
                jobs.append((clip, key, preset, frames))

    def run(job):
        clip, key, preset, frames = job
        tmp = os.path.join(video, clip, key)
        os.makedirs(tmp, exist_ok=True)
        todo = [(src, f'{tmp}/{i:04d}.png') for i, src in enumerate(frames) if not os.path.exists(f'{tmp}/{i:04d}.png')]
        if preset is None and todo:
            ours(args, key[5:], todo, (OW, OH))
        for src, dst in todo if preset else []:
            error = librashader(args, preset, src, dst, (OW, OH))
            if error:
                return f'{clip} {key} FAILED {error[-200:]}'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-framerate', '50', '-i', f'{tmp}/%04d.png', '-c:v', 'libvpx-vp9',
                        '-pix_fmt', 'yuv444p', '-crf', '12', '-b:v', '0', '-row-mt', '1', '-deadline', 'good',
                        '-cpu-used', '4', os.path.join(video, clip, f'{key}.webm')], check=True)
        return f'{clip} {key} ok'

    with ThreadPoolExecutor(args.jobs) as pool:
        for line in pool.map(run, jobs):
            print(line, flush=True)


if __name__ == '__main__':
    main()
