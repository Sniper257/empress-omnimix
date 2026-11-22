"""BMS/BME/BML reader producing arcade .1-style events. Stdlib only.

`read(path)` returns a Chart whose `events` use the same tuple layout and type
codes as arcade_data.chart_events, so one converter handles both sources:

  (time_ms, type, param, value)
  type 0/1  P1/P2 note       param = column (0..6 keys, 7 scratch), value = charge-note length in ms
  type 2/3  P1/P2 keysound   param = column, value = #WAV id; emitted 1 ms after the column's previous note
  type 4    tempo            value / param = BPM
  type 5    meter            value / param = measure length in 4/4 units (only when not 1)
  type 6    end              time of the last object
  type 7    BGM keysound     value = #WAV id
  type 12   measure bar      param = side (0, 1)
  type 16   note count       param = side, value = notes

Supported: #BPM/#BPMxx/#STOPxx, channels 01 02 03 08 09, 11-19/21-29 visible
notes, 51-59/61-69 long notes (#LNTYPE 1) and #LNOBJ. #RANDOM blocks take
branch 1 (deterministic). Invisible notes, mines and BGA layers are counted
in `ignored`/`bga` but not converted.
"""
from bisect import bisect_right
from fractions import Fraction
from pathlib import Path
import re
import shutil
import subprocess

import arcade_data as A

EXTENSIONS = ('.bms', '.bme', '.bml')
AUDIO_EXTENSIONS = ('.wav', '.ogg', '.flac', '.mp3')
# BME lane channel (second digit) -> IIDX column; 7 = scratch, 17/27 free zone is ignored
LANES = {'1': 0, '2': 1, '3': 2, '4': 3, '5': 4, '8': 5, '9': 6, '6': 7}
DIFFICULTY = {1: 'B', 2: 'N', 3: 'H', 4: 'A', 5: 'L'}
TAG = re.compile(r'[\[(](?:SP|DP)?\s*(BEGINNER|NORMAL|HYPER|ANOTHER|LEGGENDARIA|[BNHAL])[\])]', re.I)
LINE = re.compile(r'#(\d{3})([0-9A-Za-z]{2}):(.*)')


class Chart:
    def __init__(self):
        self.header = {}
        self.wav = {}
        self.events = []
        self.ignored = {}
        self.bga = 0
        self.random = False
        self.notes = (0, 0)

    @property
    def double(self):
        return self.header.get('PLAYER') == '3' or self.notes[1] > 0

    @property
    def difficulty(self):
        """'SPN', 'DPA', ...: #DIFFICULTY first, then a [N]/[H]/[A] filename-style tag in #TITLE/#SUBTITLE."""
        letter = None
        try:
            letter = DIFFICULTY.get(int(self.header.get('DIFFICULTY', '')))
        except ValueError:
            pass
        if letter is None:
            for key in ('SUBTITLE', 'TITLE'):
                m = TAG.search(self.header.get(key, ''))
                if m:
                    letter = m.group(1)[0].upper()
                    break
        return ('DP' if self.double else 'SP') + (letter or '?')


def b36(text):
    return int(text, 36)


def decode_text(raw):
    for codec in ('cp932', 'utf-8'):
        try:
            return raw.decode(codec)
        except UnicodeDecodeError:
            pass
    return raw.decode('latin-1')


def _lines(text, choice=1):
    """Yield active lines, resolving #RANDOM/#IF/#ELSE/#ENDIF with a fixed choice."""
    stack = []   # (value, active, taken)
    rnd = []
    used = False
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith('#'):
            continue
        word = line.split(None, 1)[0].upper()
        active = all(s[1] for s in stack)
        if word in ('#RANDOM', '#SETRANDOM'):
            used = True
            rnd.append(choice)
        elif word == '#ENDRANDOM':
            rnd and rnd.pop()
        elif word == '#IF':
            want = line.split(None, 1)[1].strip() if ' ' in line else ''
            hit = bool(rnd) and want == str(rnd[-1])
            stack.append([want, hit, hit])
        elif word == '#ELSEIF' and stack:
            want = line.split(None, 1)[1].strip() if ' ' in line else ''
            hit = not stack[-1][2] and bool(rnd) and want == str(rnd[-1])
            stack[-1][1] = hit
            stack[-1][2] |= hit
        elif word == '#ELSE' and stack:
            stack[-1][1] = not stack[-1][2]
            stack[-1][2] = True
        elif word in ('#ENDIF', '#END') and stack:
            stack.pop()
        elif active:
            yield line
    yield used


class Timeline:
    """Beat -> milliseconds with tempo changes and stops (objects on a stop's beat play before it)."""

    def __init__(self, bpm, changes, stops):
        self.points = [(Fraction(0), 0.0, bpm)]
        for beat, value in sorted(changes):
            b0, ms0, old = self.points[-1]
            ms = ms0 + float(beat - b0) * 60000.0 / old
            if beat == b0:
                self.points[-1] = (beat, ms0, value)
            else:
                self.points.append((beat, ms, value))
        self.beats = [p[0] for p in self.points]
        self.stops = []
        total = 0.0
        for beat, length in sorted(stops):
            total += float(length) * 60000.0 / self.bpm_at(beat)
            self.stops.append((beat, total))
        self.stop_beats = [s[0] for s in self.stops]

    def bpm_at(self, beat):
        return self.points[bisect_right(self.beats, beat) - 1][2]

    def ms(self, beat):
        b0, ms0, bpm = self.points[bisect_right(self.beats, beat) - 1]
        i = bisect_right(self.stop_beats, beat) - 1
        # stops strictly before this beat delay it
        while i >= 0 and self.stops[i][0] == beat:
            i -= 1
        return ms0 + float(beat - b0) * 60000.0 / bpm + (self.stops[i][1] if i >= 0 else 0.0)


def parse(text):
    chart = Chart()
    rows = []
    lengths = {}
    bpm_defs, stop_defs = {}, {}
    lnobj = None
    items = list(_lines(text))
    chart.random = items.pop()
    for line in items:
        m = LINE.fullmatch(line)
        if m:
            measure, channel, data = int(m.group(1)), m.group(2).upper(), m.group(3).strip()
            if channel == '02':
                try:
                    lengths[measure] = Fraction(data).limit_denominator(1 << 16)
                except (ValueError, ZeroDivisionError):
                    raise A.FormatError('Invalid measure length: ' + line)
            else:
                rows.append((measure, channel, re.sub(r'\s', '', data)))
            continue
        key, _, value = line[1:].partition(' ')
        key, value = key.upper(), value.strip()
        if key.startswith('WAV') and len(key) == 5:
            chart.wav[b36(key[3:])] = value
        elif key.startswith('BPM') and len(key) == 5:
            bpm_defs[b36(key[3:])] = float(value)
        elif key.startswith('STOP') and len(key) == 6:
            stop_defs[b36(key[4:])] = int(value)
        elif key == 'LNOBJ':
            lnobj = b36(value)
        elif key.startswith('BMP') and len(key) == 5:
            chart.bga += 1
        else:
            chart.header[key] = value
    if chart.header.get('LNTYPE', '1') not in ('1', ''):
        raise A.FormatError('Only #LNTYPE 1 long notes are supported')
    try:
        bpm = float(chart.header.get('BPM', '130'))
    except ValueError:
        raise A.FormatError('Invalid #BPM')
    last = max([r[0] for r in rows] + [0])
    starts = [Fraction(0)]
    for measure in range(last + 1):
        starts.append(starts[-1] + 4 * lengths.get(measure, Fraction(1)))

    def beat(measure, i, n):
        return starts[measure] + 4 * lengths.get(measure, Fraction(1)) * Fraction(i, n)

    objects, changes, stops = [], [], []
    for measure, channel, data in rows:
        if len(data) % 2:
            raise A.FormatError('Odd channel data length in measure %d' % measure)
        n = len(data) // 2
        for i in range(n):
            pair = data[2 * i:2 * i + 2]
            if pair == '00':
                continue
            b = beat(measure, i, n)
            if channel == '03':
                changes.append((b, float(int(pair, 16))))
            elif channel == '08':
                if b36(pair) in bpm_defs:
                    changes.append((b, bpm_defs[b36(pair)]))
            elif channel == '09':
                if b36(pair) in stop_defs:
                    stops.append((b, Fraction(stop_defs[b36(pair)], 48)))
            else:
                objects.append((b, channel, b36(pair)))
    time = Timeline(bpm, changes, stops)
    events = []
    current = {}          # (side, column) -> wav id
    previous = {}         # (side, column) -> time of the previous note
    open_ln = {}
    last_note = {}        # (side, column) -> index of the last note event (for #LNOBJ)
    notes = [0, 0]
    for b, channel, value in sorted(objects, key=lambda o: (o[0], o[1])):
        ms = time.ms(b)
        kind = channel[0]
        if channel == '01':
            events.append((ms, 7, 0, value))
            continue
        if kind in '1256' and channel[1] in LANES:
            side = 0 if kind in '15' else 1
            column = LANES[channel[1]]
            key = (side, column)
            if kind in '12' and lnobj is not None and value == lnobj and key in last_note:
                at = last_note.pop(key)
                t, k, p, _ = events[at]
                events[at] = (t, k, p, max(1, round(ms - t)))
                continue
            if kind in '56' and key in open_ln:
                at = open_ln.pop(key)
                t, k, p, _ = events[at]
                events[at] = (t, k, p, max(1, round(ms - t)))
                continue
            if current.get(key) != value:
                # just after the column's previous note, so that note keeps its own sound
                at = previous[key] + 1 if key in previous and previous[key] + 1 <= ms else (ms if key in previous else 0.0)
                events.append((at, 2 + side, column, value))
                current[key] = value
            events.append((ms, side, column, 0))
            notes[side] += 1
            previous[key] = ms
            if kind in '56':
                open_ln[key] = len(events) - 1
            else:
                last_note[key] = len(events) - 1
            continue
        if channel in ('04', '06', '07'):
            continue
        chart.ignored[channel] = chart.ignored.get(channel, 0) + 1
    if open_ln:
        raise A.FormatError('Unterminated long note')
    chart.notes = tuple(notes)
    for b, value in [(Fraction(0), bpm)] + sorted(changes):
        for param in (100, 10, 1):
            if round(value * param) < 32768:
                events.append((time.ms(b), 4, param, round(value * param)))
                break
    for measure in range(last + 2):
        ms = time.ms(starts[measure])
        for side in (0, 1):
            events.append((ms, 12, side, 0))
        length = lengths.get(measure, Fraction(1))
        if length != 1 and measure <= last:
            f = length.limit_denominator(64)
            events.append((ms, 5, f.denominator, f.numerator))
    end = max([e[0] + (e[3] if e[1] in (0, 1) else 0) for e in events if e[1] not in (5, 12)] + [0.0])
    events.append((end, 6, 0, 0))
    for side in (0, 1):
        events.append((0.0, 16, side, notes[side]))
    order = {16: 0, 4: 1, 5: 2, 12: 3, 2: 4, 3: 4, 7: 5, 0: 6, 1: 6, 6: 7}
    chart.events = sorted(((max(0, round(t)), k, p, v) for t, k, p, v in events), key=lambda e: (e[0], order.get(e[1], 9)))
    return chart


def read(path):
    return parse(decode_text(Path(path).read_bytes()))


def song_folders(root):
    """{folder: [chart files]} for every folder under root that holds BMS charts."""
    out = {}
    for path in sorted(Path(root).rglob('*')):
        if path.is_file() and path.suffix.lower() in EXTENSIONS:
            out.setdefault(path.parent, []).append(path)
    return out


def find_audio(folder, name, index=None):
    """Resolve a #WAV name case-insensitively, trying the other usual audio extensions."""
    if index is None:
        index = {p.name.lower(): p for p in Path(folder).iterdir() if p.is_file()}
    stem = name.replace('\\', '/').rsplit('.', 1)[0].lower()
    for ext in ('.' + name.rsplit('.', 1)[-1].lower(),) + AUDIO_EXTENSIONS:
        hit = index.get(stem.split('/')[-1] + ext)
        if hit:
            return hit
    return None


def decode_audio(path, ffmpeg=None):
    """(rate, channels) from a WAV (stdlib) or, with ffmpeg on PATH/given, any other audio file."""
    path = Path(path)
    if path.suffix.lower() == '.wav':
        return A.decode_wav(path.read_bytes())
    tool = ffmpeg or shutil.which('ffmpeg')
    if not tool:
        raise A.FormatError(path.suffix + ' keysounds need ffmpeg (or convert them to WAV first)')
    raw = subprocess.run([tool, '-v', 'error', '-i', str(path), '-f', 's16le', '-ac', '2', '-ar', '44100', '-'],
                         check=True, capture_output=True).stdout
    import array
    pcm = array.array('h')
    pcm.frombytes(raw[:len(raw) // 4 * 4])
    return 44100, [list(pcm[0::2]), list(pcm[1::2])]
