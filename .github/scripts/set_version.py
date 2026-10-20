#!/usr/bin/env python3
"""Set the VLCBlib_PIC version for a release.

    set_version.py VERSION [--notes FILE] [--date YYYY-MM-DD]

VERSION is MAJOR.MINOR.PATCH, optionally followed by a pre-release such as
-rc1, -alpha2 or -beta3 (e.g. 1.0.0 or 1.0.0-rc1).

- vlcb_version.h gets the new numbers and string.
- Doxyfile gets PROJECT_NUMBER.
- CHANGELOG.md, if it has an "## Unreleased" section: that section is the
  release notes (written to --notes). For a release (not a pre-release) the
  section is renamed to the version and date, and a new empty Unreleased
  section is started above it.

Refuses a version that is not newer than the one in vlcb_version.h. Run from the
repository root. Used by .github/workflows/release.yml.
"""
import argparse, datetime, re, sys

VERSION_RE = re.compile(r'^(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)(\d+))?$')
HEADER = 'vlcb_version.h'


def parse(v):
    m = VERSION_RE.match(v)
    if not m:
        sys.exit('Version must look like 1.2.3 or 1.2.3-rc1 (alpha, beta or rc), not %r' % v)
    major, minor, patch = (int(m.group(i)) for i in (1, 2, 3))
    if major > 255 or minor > 255 or patch > 255:
        sys.exit('Each part of the version must be 0..255')
    pre = int(m.group(5)) if m.group(5) else 0
    if m.group(4) and pre == 0:
        sys.exit('A pre-release number starts at 1, e.g. -rc1')
    label = m.group(4) or ''
    return major, minor, patch, label, pre


def order(major, minor, patch, label, pre):
    """Sort key: a pre-release comes before its release; alpha < beta < rc."""
    rank = {'alpha': 0, 'beta': 1, 'rc': 2, '': 3}[label]
    return (major, minor, patch, rank, pre)


def current(text):
    def num(name):
        m = re.search(r'^#define\s+%s\s+(\d+)' % name, text, re.M)
        if not m:
            sys.exit('%s: %s not found' % (HEADER, name))
        return int(m.group(1))
    s = re.search(r'^#define\s+VLCBLIB_VERSION_STRING\s+"([^"]*)"', text, re.M)
    if not s:
        sys.exit('%s: VLCBLIB_VERSION_STRING not found' % HEADER)
    return s.group(1), (num('VLCBLIB_VERSION_MAJOR'), num('VLCBLIB_VERSION_MINOR'),
                        num('VLCBLIB_VERSION_PATCH'), num('VLCBLIB_VERSION_PRERELEASE'))


def sub_once(pattern, repl, text, what):
    new, n = re.subn(pattern, repl, text, count=1, flags=re.M)
    if n != 1:
        sys.exit('%s not found' % what)
    return new


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('version')
    ap.add_argument('--notes', help='write the release notes here')
    ap.add_argument('--date', default=datetime.date.today().isoformat())
    a = ap.parse_args()

    major, minor, patch, label, pre = parse(a.version)

    text = open(HEADER, newline='').read()
    old_string, _ = current(text)
    if old_string != '0.0.0' and order(major, minor, patch, label, pre) <= order(*parse(old_string)):
        sys.exit('%s is not newer than the current version %s' % (a.version, old_string))

    for name, value in (('MAJOR', major), ('MINOR', minor), ('PATCH', patch), ('PRERELEASE', pre)):
        text = sub_once(r'^(#define\s+VLCBLIB_VERSION_%s\s+)\d+' % name, r'\g<1>%d' % value, text,
                        'VLCBLIB_VERSION_%s' % name)
    text = sub_once(r'^(#define\s+VLCBLIB_VERSION_STRING\s+)"[^"]*"', r'\g<1>"%s"' % a.version, text,
                    'VLCBLIB_VERSION_STRING')
    open(HEADER, 'w', newline='').write(text)

    try:
        dox = open('Doxyfile', newline='').read()
        dox = sub_once(r'^(PROJECT_NUMBER\s*=).*$', r'\g<1> %s' % a.version, dox, 'Doxyfile PROJECT_NUMBER')
        open('Doxyfile', 'w', newline='').write(dox)
    except FileNotFoundError:
        pass

    notes = ''
    try:
        log = open('CHANGELOG.md', newline='').read()
        m = re.search(r'^## Unreleased[ \t]*\r?\n(.*?)(?=^## |\Z)', log, re.M | re.S)
        if m:
            notes = m.group(1).strip()
            if not label:
                nl = '\r\n' if '\r\n' in log else '\n'
                log = (log[:m.start()] + '## Unreleased' + nl + nl +
                       '## v%s - %s' % (a.version, a.date) + nl + nl +
                       (notes + nl + nl if notes else '') + log[m.end():])
                open('CHANGELOG.md', 'w', newline='').write(log)
    except FileNotFoundError:
        pass
    if a.notes:
        open(a.notes, 'w').write(notes + ('\n' if notes else ''))

    print('VLCBlib_PIC %s -> %s' % (old_string, a.version))


if __name__ == '__main__':
    main()

