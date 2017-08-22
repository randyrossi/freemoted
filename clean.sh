# Remove all the artifacts automatically generated using automake tools
# and get us back to what we extracted from source tree.

read -p "Remove aclocal, autoconf, automake artifacts? (y/n):"

if [ "$REPLY" = "y" -o "$REPLY" = "Y" ]
then
echo Removing...
find . -name 'Makefile' -exec rm -f {} \;
find . -name '*.o' -exec rm -f {} \;
find . -name '*.so' -exec rm -f {} \;
find . -name '*.la' -exec rm -f {} \;
find . -name '*.lo' -exec rm -f {} \;
find . -name '*.loT' -exec rm -f {} \;
find . -name 'Makefile.in' -exec rm -f {} \;
find . -name '.deps' -exec rm -rf {} \;
find . -name '.libs' -exec rm -rf {} \;
rm -f clean
rm -f ltmain.sh
rm -f config.sub
rm -f config.guess
rm -f install-sh
rm -f configure
rm -f aclocal.m4
rm -rf autom4te.cache
rm -f missing
rm -f depcomp
rm -f config.h
rm -f config.log
rm -f config.status
rm -f libtool
rm -f stamp-h1
rm -f config.h.in~
rm -f autoscan.log
rm -rf m4
rm -f INSTALL COPYING
rm -f src/stamp-h1
rm -f src/config.h
# These are from Win32 Visual Studio Development
rm -rf src/ipch
rm -rf src/Debug
rm -rf src/Release
rm -f src/Freemote\ Control.suo
rm -f src/Freemote\ Control.sdf
rm -f src/Freemote\ Control.vcxproj.user
rm -rf src/build
fi
