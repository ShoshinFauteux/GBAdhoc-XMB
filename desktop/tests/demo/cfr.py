"""cfr.py <frames dir> <out.mp4> [fps] -- screencast frames (frames.txt: file timestamp)
to a constant-rate master: each frame held until the next one's timestamp."""
import os, subprocess, sys

d, out = sys.argv[1], sys.argv[2]
fps = int(sys.argv[3]) if len(sys.argv) > 3 else 60
rows = [l.split() for l in open(os.path.join(d, 'frames.txt')) if l.strip()]
rows = [(f, float(t)) for f, t in rows]
lst = os.path.join(d, 'concat.txt')
with open(lst, 'w') as fh:
    for i, (f, t) in enumerate(rows):
        dur = (rows[i + 1][1] - t) if i + 1 < len(rows) else 0.5
        fh.write(f"file '{f}'\nduration {max(dur, 0.001):.4f}\n")
    fh.write(f"file '{rows[-1][0]}'\n")
span = rows[-1][1] - rows[0][1]
gaps = sorted((rows[i + 1][1] - rows[i][1] for i in range(len(rows) - 1)), reverse=True)[:5]
print(f'{len(rows)} frames over {span:.2f} s; longest holds {[round(g, 3) for g in gaps]}')
subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', lst,
                '-vf', f'fps={fps},format=yuv444p', '-c:v', 'libx264', '-preset', 'slow', '-crf', '6', out], check=True)
print(out)
