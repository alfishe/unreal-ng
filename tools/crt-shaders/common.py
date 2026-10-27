"""Options and helpers the gallery tools share: the work directory (git-ignored, default scratch/crt-gallery), the
librashader-cli and slang-shaders locations, unreal-qt's crt-render."""
import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
FRAMES = os.path.join(ROOT, 'testdata', 'video', 'crt-gallery')


def parser(description, cli=False, render=False):
    p = argparse.ArgumentParser(description=description)
    p.add_argument('--work', default=os.path.join(ROOT, 'scratch', 'crt-gallery'),
                   help='work directory: out/, clips/, video/, the HTML pages (default: scratch/crt-gallery)')
    if cli:
        p.add_argument('--cli', default=os.environ.get('LIBRASHADER_CLI', 'librashader-cli'),
                       help='librashader-cli (default: $LIBRASHADER_CLI or the PATH)')
        p.add_argument('--slang', default=os.environ.get('SLANG_SHADERS'),
                       help='libretro slang-shaders checkout (default: $SLANG_SHADERS)')
        p.add_argument('--runtime', default='metal' if sys.platform == 'darwin' else 'vk',
                       help='librashader-cli runtime (default: metal on macOS, vk elsewhere)')
    if render:
        p.add_argument('--render', default=os.path.join(ROOT, 'cmake-build-agent-release', 'bin', 'crt-render'),
                       help='unreal-qt\'s crt-render (tools/build/build.sh crt-render)')
    return p


def librashader(args, preset, src, dst, size, frame=None):
    """One frame through a preset with librashader-cli; returns the error text or None"""
    if not args.slang:
        sys.exit('--slang (or $SLANG_SHADERS) is needed: a libretro slang-shaders checkout')
    command = [args.cli, 'render', '-p', os.path.join(args.slang, preset), '-i', src, '-o', dst, '-r', args.runtime,
               '-d', f'{size[0]}x{size[1]}']
    if frame is not None:
        command += ['-f', str(frame)]
    r = subprocess.run(command, capture_output=True, text=True)
    if r.returncode or not os.path.exists(dst):
        return (r.stderr or r.stdout)[-300:]
    return None


def ours(args, profile, pairs, size):
    """unreal-qt's picture of (in.png, out.png) pairs for a profile key"""
    command = [args.render, profile, f'{size[0]}x{size[1]}']
    for src, dst in pairs:
        command += [src, dst]
    subprocess.run(command, check=True)
