# suq

A keyboard-driven speaker queue for meetings, built with raylib.

People with fewer turns are placed ahead of those with more.
Sub-motions open a nested queue with its own turn counts; closing one returns to the queue above.

## Build

raylib is vendored as a git submodule and built from source on the first run.

```sh
git submodule update --init
./build.sh      # builds build/suq
./test.sh       # queue logic tests, no window needed
```

Remove `build/raylib` to rebuild raylib after updating the submodule.

## Use

|Key                    |Action                                                                                               |
|-----------------------|-----------------------------------------------------------------------------------------------------|
|`Enter`                |Add the highlighted suggestion, or what you typed                                                    |
|`Shift+Enter`          |Add exactly what you typed                                                                           |
|`Tab` / `→`            |Complete the name                                                                                    |
|`Space` (empty prompt) |End this turn and call the next speaker                                                              |
|`↑` `↓` (empty prompt) |Select someone waiting; `Enter` gives them the floor, `Backspace` removes them, `Shift+↑↓` moves them|
|`Cmd+]` / `Cmd+[`      |Open / close a sub-motion                                                                            |
|`Cmd+Z` / `Shift+Cmd+Z`|Undo / redo                                                                                          |
|`Cmd+D`                |Stop suggesting the highlighted name                                                                 |
|`Cmd+R`                |Start fresh                                                                                          |
|`Cmd+=` / `Cmd+-`      |Zoom                                                                                                 |
|`Cmd+F`                |Full screen                                                                                          |
|`?`                    |All keys                                                                                             |

Use `Ctrl` instead of `Cmd` on Linux and Windows.

State is saved after every change to `~/Library/Application Support/suq/`
(`$XDG_STATE_HOME/suq` on Linux, `%APPDATA%\suq` on Windows), or to the path in
`$SUQ_STATE`. `$SUQ_FONT` and `$SUQ_FONT_MONO` override the fonts.
