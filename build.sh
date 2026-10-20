#!/bin/sh
# Builds build/suq, compiling the vendored raylib on first use.
# Remove build/raylib to force raylib to be rebuilt.
set -eu

cd "$(dirname "$0")"

CC=${CC:-cc}
CFLAGS=${CFLAGS:-"-std=c99 -O2 -Wall -Wextra -Wpedantic"}
RAYLIB=vendor/raylib/src
OUT=build/raylib

if [ ! -f "$RAYLIB/raylib.h" ]; then
	git submodule update --init vendor/raylib
fi

case "$(uname)" in
Darwin)
	GLFW_FLAGS="-x objective-c"
	PLATFORM_CFLAGS=""
	LIBS="-framework OpenGL -framework Cocoa -framework IOKit
	    -framework CoreAudio -framework CoreVideo -framework QuartzCore"
	;;
Linux)
	GLFW_FLAGS=""
	PLATFORM_CFLAGS="-D_GNU_SOURCE"
	LIBS="-lGL -lm -lpthread -ldl -lrt -lX11"
	;;
*BSD|DragonFly)
	GLFW_FLAGS=""
	PLATFORM_CFLAGS="-I/usr/X11R6/include -I/usr/local/include"
	LIBS="-L/usr/X11R6/lib -L/usr/local/lib -lGL -lX11 -lm -lpthread"
	;;
*)
	echo "build.sh: unsupported platform $(uname)" >&2
	exit 1
	;;
esac

if [ ! -f "$OUT/libraylib.a" ]; then
	echo "building raylib"
	mkdir -p "$OUT"
	pids=""
	for m in rcore rshapes rtextures rtext rmodels raudio rglfw; do
		flags=""
		if [ "$m" = rglfw ]; then
			flags=$GLFW_FLAGS
		fi
		# shellcheck disable=SC2086
		$CC -O2 -w -DPLATFORM_DESKTOP_GLFW $PLATFORM_CFLAGS \
		    -I"$RAYLIB" -I"$RAYLIB/external/glfw/include" \
		    $flags -c "$RAYLIB/$m.c" -o "$OUT/$m.o" &
		pids="$pids $!"
	done
	for pid in $pids; do
		wait "$pid"
	done
	ar rcs "$OUT/libraylib.a" "$OUT"/*.o
fi

# shellcheck disable=SC2086
$CC $CFLAGS $PLATFORM_CFLAGS -I"$RAYLIB" src/main.c src/queue.c \
    -o build/suq "$OUT/libraylib.a" $LIBS -lm
echo "built build/suq"
