const aliases: Record<string, string> = {
  arrowup: 'Up', arrowdown: 'Down', arrowleft: 'Left', arrowright: 'Right',
  delete: 'Del', del: 'Del', escape: 'Esc', esc: 'Esc', insert: 'Ins', ins: 'Ins',
  backspace: 'Backspace', enter: 'Return', return: 'Return', tab: 'Tab', backtab: 'Tab',
  home: 'Home', end: 'End', pageup: 'PgUp', pgup: 'PgUp', pagedown: 'PgDown', pgdown: 'PgDown',
  printscreen: 'Print', print: 'Print', pause: 'Pause', ' ': 'Space', space: 'Space',
};

const modifiers = ['Ctrl', 'Alt', 'Shift', 'Meta', 'Num'];

function normalizeKey(key: string): string {
  return aliases[key.toLowerCase()] ?? (/^(?:.|F(?:[1-9]|1\d|2[0-4]))$/i.test(key) ? key.toUpperCase() : '');
}

type ShortcutEvent = Pick<KeyboardEvent, 'key' | 'code' | 'ctrlKey' | 'altKey' | 'shiftKey' | 'metaKey' | 'location'>;

export function keyEventToPortable(event: ShortcutEvent): string | null {
  // Qt writes the base digit for Shift+digit while the DOM reports its shifted symbol.
  const key = normalizeKey(event.shiftKey && /^Digit[0-9]$/.test(event.code)
    ? event.code.slice(5)
    : event.key);
  if (!key) return null;
  const mods = [event.ctrlKey, event.altKey, event.shiftKey, event.metaKey, event.location === 3];
  return [...modifiers.filter((_, index) => mods[index]), key].join('+');
}

function normalizePortable(chord: string): string | null {
  const tokens = chord.endsWith('+') ? [...chord.slice(0, -1).split('+').filter(Boolean), '+'] : chord.split('+');
  const keyName = tokens.pop()?.trim();
  const key = normalizeKey(keyName ?? '');
  const mods = new Set(tokens.map((token) => token.trim().toLowerCase()));
  if (keyName?.toLowerCase() === 'backtab') mods.add('shift');
  if (!key || [...mods].some((mod) => !modifiers.some((name) => mod === name.toLowerCase()))) return null;
  return [...modifiers.filter((mod) => mods.has(mod.toLowerCase())), key].join('+');
}

export const TEXT_INPUT_SELECTOR = 'input, textarea, select, [contenteditable]:not([contenteditable="false"])';

export function canHandleShortcut(event: KeyboardEvent): boolean {
  return !event.defaultPrevented && !event.isComposing &&
    !(event.target instanceof Element && event.target.closest(
      `${TEXT_INPUT_SELECTOR}, .shortcut-capture-btn.recording`
    ));
}

export function createShortcutMatcher<Id extends string>(
  bindings: Record<string, string>,
  ids: readonly Id[],
) {
  const sequences = ids.flatMap((id) => {
    const chords = bindings[id]?.trim().split(/,\s+/).map(normalizePortable);
    return chords?.every((key) => key !== null) ? [{ id, chords }] : [];
  });
  let prefix: string[] = [];
  let previousStroke = 0;

  return (event: KeyboardEvent): Id | undefined => {
    const chord = keyEventToPortable(event);
    if (!chord) return;
    if (event.timeStamp - previousStroke > 1000) prefix = [];
    previousStroke = event.timeStamp;
    const findMatches = (strokes: string[]) => sequences.filter(({ chords }) =>
      strokes.length <= chords.length && strokes.every((stroke, index) => stroke === chords[index])
    );
    prefix.push(chord);
    let matches = findMatches(prefix);
    if (matches.length === 0 && prefix.length > 1) {
      prefix = [chord];
      matches = findMatches(prefix);
    }
    const match = matches.find(({ chords }) => chords.length === prefix.length);
    if (match || matches.length === 0) prefix = [];
    if (matches.length > 0) event.preventDefault();
    return match?.id;
  };
}
