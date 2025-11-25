CFLAGS = -Wall
CFLAGS += $(shell sdl2-config --cflags) 
CFLAGS += $(shell pkg-config --cflags json-c libcurl gl glew xcb)
CFLAGS += $(shell xml2-config --cflags)

CFLAGS += -pthread

LIBS += $(shell sdl2-config --libs) 
LIBS += $(shell pkg-config --libs json-c gl glew libmurmurhash libcurl x11 xcb) -pthread
LIBS += $(shell xml2-config --libs)

PROG = offblast
OBJS = main.o offblastDbFile.o

#TODO Optimization on for production!

${PROG}: ${OBJS}
	gcc -g -o ${PROG} ${OBJS} -lm ${LIBS} 

main.o: main.c offblast.h offblastDbFile.h shaders/*
	gcc -g -c ${CFLAGS} main.c

offblastDbFile.o: offblastDbFile.c offblast.h offblastDbFile.h
	gcc -g -c  offblastDbFile.c

clean:
	rm -f ./*.o
	rm -f ${PROG}

install:
	mkdir -p ~/.offblast
	cp -i config-dist.json ~/.offblast/config.json

appimage: ${PROG}
	@echo "Creating AppImage build directory..."
	rm -rf AppImageBuild
	mkdir -p AppImageBuild/offblast.AppDir/usr/bin
	mkdir -p AppImageBuild/offblast.AppDir/usr/lib

	@echo "Copying binary..."
	cp ${PROG} AppImageBuild/offblast.AppDir/usr/bin/

	@echo "Copying fonts and shaders..."
	cp -r fonts AppImageBuild/offblast.AppDir/usr/bin/
	cp -r shaders AppImageBuild/offblast.AppDir/usr/bin/

	@echo "Copying resource files..."
	cp guest-512.jpg AppImageBuild/offblast.AppDir/usr/bin/
	cp missingcover.png AppImageBuild/offblast.AppDir/usr/bin/

	@echo "Copying OpenGameDB..."
	@if [ -d "../opengamedb" ]; then \
		cp -r ../opengamedb AppImageBuild/offblast.AppDir/usr/bin/; \
		echo "  OpenGameDB included in AppImage"; \
	else \
		echo "  Warning: ../opengamedb not found, AppImage will not include database"; \
	fi

	@echo "Copying dependencies..."
	@# Get all library dependencies except system base libraries
	@# We exclude libc, libm, libdl, libpthread, librt, libGL, libX11, etc. as they should use system versions
	@ldd ${PROG} | grep "=>" | awk '{print $$3}' | grep -v "^$$" | \
		grep -v -E '(libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libgcc_s\.so|libstdc\+\+\.so|ld-linux|linux-vdso)' | \
		grep -v -E '(libGL\.so|libGLX\.so|libEGL\.so|libdrm\.so|libgbm\.so|libGLdispatch\.so)' | \
		grep -v -E '(libX11\.so|libxcb\.so|libXext\.so|libXau\.so|libXdmcp\.so|libwayland)' | \
		grep -v -E '(libssl\.so|libcrypto\.so|libgnutls\.so|libgssapi|libkrb5|libnettle\.so|libhogweed\.so)' | \
	while read lib; do \
		if [ -f "$$lib" ]; then \
			echo "  Bundling $$lib"; \
			cp "$$lib" AppImageBuild/offblast.AppDir/usr/lib/ 2>/dev/null || true; \
		fi \
	done

	@echo "Creating AppRun script..."
	@echo '#!/bin/bash' > AppImageBuild/offblast.AppDir/AppRun
	@echo 'SELF=$$(readlink -f "$$0")' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'HERE=$${SELF%/*}' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '# Desktop integration - update on every run in case AppImage was moved' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '# Use APPIMAGE env var if available (points to actual .AppImage file), otherwise use ARGV0' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'APPIMAGE_PATH="$${APPIMAGE:-$$(readlink -f "$$ARGV0")}"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'if [ -n "$$APPIMAGE_PATH" ] && [ -f "$$APPIMAGE_PATH" ]; then' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    mkdir -p "$$HOME/.local/share/applications"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    mkdir -p "$$HOME/.cache/offblast"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    cp "$${HERE}/offblast.png" "$$HOME/.cache/offblast/icon.png"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    sed -e "s|Exec=offblast|Exec=\"$$APPIMAGE_PATH\"|" -e "s|Icon=offblast|Icon=$$HOME/.cache/offblast/icon.png|" "$${HERE}/offblast.desktop" > "$$HOME/.local/share/applications/offblast.desktop"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$$HOME/.local/share/applications" 2>/dev/null || true' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'fi' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '# Sync OpenGameDB if bundled version is newer' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'if [ -d "$${HERE}/usr/bin/opengamedb" ]; then' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    mkdir -p "$$HOME/.offblast"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    if [ ! -d "$$HOME/.offblast/opengamedb" ] || [ "$${HERE}/usr/bin/opengamedb" -nt "$$HOME/.offblast/opengamedb" ]; then' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '        echo "Updating OpenGameDB..."' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '        cp -r "$${HERE}/usr/bin/opengamedb" "$$HOME/.offblast/"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '    fi' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'fi' >> AppImageBuild/offblast.AppDir/AppRun
	@echo '' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'export LD_LIBRARY_PATH="$${HERE}/usr/lib:$${LD_LIBRARY_PATH}"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'cd "$${HERE}/usr/bin"' >> AppImageBuild/offblast.AppDir/AppRun
	@echo 'exec "./offblast" "$$@"' >> AppImageBuild/offblast.AppDir/AppRun
	chmod +x AppImageBuild/offblast.AppDir/AppRun

	@echo "Copying desktop file and icon..."
	cp offblast.png AppImageBuild/offblast.AppDir/
	@# Fix desktop file paths for AppImage
	sed -e 's|^Path=.*||' -e 's|^Exec=.*|Exec=offblast|' -e 's|^Icon=.*|Icon=offblast|' \
		offblast.desktop > AppImageBuild/offblast.AppDir/offblast.desktop
	@# Add StartupWMClass for proper dock icon
	@echo 'StartupWMClass=offblast' >> AppImageBuild/offblast.AppDir/offblast.desktop

	@echo "Packaging AppImage..."
	cd AppImageBuild && appimagetool offblast.AppDir
	@echo "Done! AppImage created at AppImageBuild/Offblast-x86_64.AppImage"

