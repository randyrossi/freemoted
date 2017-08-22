freemoted

LICENSE

   Freemote Control Proxy
   Copyright 2010 by Randy Rossi
   This program is under GNU General Public License.

VERSION

   1.4

SUPPORTED PLATFORMS

   Linux
   Windows XP+SP2, Windows Vista, Windows 7
   MAC OSX 10.6

ABOUT

   freemoted is TCP socket server that lets clients inject keys and/or mouse
   movement events into the OS's windowing subsystem.  Linux, Windows and
   Mac are supported.

   For Linux systems, MythTV/Boxee or any other LIRC compatible application
   (mplayer, xine, etc) may be controlled as well.  XWindows support allows
   keyboard and mouse movement control.

   For Windows or MAC OSX 10.6, only keyboard and mouse control is
   supported but you can also control applications that have focus by
   configuring the correct keystrokes into the proxy's configuration file.

CONNECTION METHODS

   freemoted primarily acts as a TCP socket server.  However, bluetooth
   connections are also supported.

   The -t options specifies which type of connection the proxy will expect:

   ie. -t socket (default) or -t bluetooth

CONTROL METHODS

   There are a number of control options the proxy can use to relay commands
   to your applications.

   All control methods are optional, but you need at least one for anything
   to function.  The more you configure, the more functionality you will get
   out of freemoted.

   The supported control methods are:

       1) Lircd simulated IR events (Linux only)
       2) Desktop mouse/key events
       3) Myth Frontend socket protocol (Linux only)

   Others methods may be added in future releases.

PREREQUISITES

   For bluetooth connectivity, you will need a working bluetooth protocol
   stack on your linux box.  These packages will be required:

        bluez
        bluez-utils
        libbluetooth

        * Version 4.32 was used for development but earlier releases may work.

   If you are compiling freemoted yourself, you will need at least these
   packages:

        build-essentials (sometimes named build-essential)
        libbluetooth-dev
        x11proto-core-dev
        libxtst-dev

   On Linux/Cygwin, you will need a build environment with g++ and make.  I may
   provide a precompiled binary for certain systems but it's best to build
   the proxy yourself.

   The precompiled MAC OSX and Windows versions will run without any
   additional steps.

   For Lirc clients on linux (MythTV, mplayer, xine, etc), you will need to
   run a lircd daemon with the --allow-simulate and --listen=<port> options
   for the proxy to create the necessary simulated IR events.

   If you don't have lirc installed, type this:

    sudo apt-get install lirc

   Make sure the START_LIRCD="true" in /etc/lirc/hardware.conf

   Next, edit the /etc/init.d/lirc file as root and add this line at the
   end of the build_remote_args() function, just before the echo statement:

    REMOTE_ARGS="--allow-simulate --listen=8888 $REMOTE_ARGS"

   Then, restart lirc by doing the following:

    sudo /etc/init.d/lirc restart

   NOTE: Some users have reported the above command failing on recent Ubuntu
   systems complaining that /var/run/lirc does not exist.  Use your root
   account to 'mkdir /var/run/lirc' and try again.

   The lirc control method is activated using the '-m lirc' command line
   option.  The host and port for your lirc daemon should be specified through
   -i and -j options unless they are the default (localhost/8888).

   For X-Windows mouse/keyboard control, you will need an XWindows server
   with the XTest extension available.  However, if you only intend to
   control applications that may already be controlled with Lirc, you do
   not need this option.  The XWindow support is activated by default. If
   you need to run without xwindows support, use '-m nodesktop'.  The display
   to connect to can be set via -x host:0.0 unless it is the default (:0.0).
   If you want keyboard and mouse control, you will need the desktop control
   method active.

   Windows and MAC OSX mouse/keyboard control is also active by default.
   It can be explicitly disabled using '-m nodesktop'.

   There are also extra MythTV control options (i.e. query what's on channels
   and/or what's been recorded) that will only be available if you turn
   on the network control protocol available in the general settings menu
   of the Myth FrontEnd application.  Turn on myth front end control
   using the '-m myth' option.  You may also specify the host and port
   for the myth front end unless they are the defaults (localhost, 6546).
   Use -h host and -p port.

WHAT METHOD GETS USED AND WHEN

   For buttons, the freemoted.conf file specifies the order for which methods
   are selected.  If the method indicated is not available, it will move on to
   the next in the list.

   For example:

       btn ok,key(enter),lirc(OK),myth(key enter)

   This line instructs the proxy what to do when the 'Ok' button is pressed
   on the smartphone.  The proxy will first prefer the enter 'key' if keyboard
   injection is available.  If not, it will attempt to simulate a LIRC 'OK'
   command.  Any application that currently has focus will receive the
   instruction as it is configured in your lircrc file.  Finally, if none of
   the previous methods are available, use the MythTV FrontEnd connection
   protocol to tell myth to execute its 'enter' function.

   Please make sure your lircrc file matches the strings found in this file
   (or edit it to match your existing strings).  See freemoted.conf for an
   explanation of the format.

   For key strokes, desktop event injection is preferred.  However, if that
   is not configured, key events will be transmitted to Myth via the
   FrontEnd connection protocol if configured.

   Mouse movement requires desktop control method.

   The shell option is always available.  On Linux, if the program you are
   executing requires an X display, make sure you export your DISPLAY
   environment variable before launching freemoted so the child process
   can connect to your display.

   Four 'custom' buttons are provided.  You may configure any application
   to execute using the shell command.  For example, I map my custom1 button
   to start Boxee:

   btn custom1,shell(/opt/boxee/run-boxee-desktop &)

BUILD FROM SOURCE (Linux/Windows+cygwin/Windows+Mac)

   ./configure
   make
   make install

SPECIAL BLUETOOTH INSTRUCTIONS FOR LINUX

   If you are using bluetooth on Linux, the following command must be
   executed at system startup:

          sdptool add --channel=1 SP

   This adds the 'Serial Port' service and associates it with channel 1.
   You must provide freemoted with the same channel number upon startup.

   After pairing with your phone, make sure the 'Serial Port' service
   shows up in the services list inside the properties for the bluetooth
   paired device.  If it doesn't show up, try using the 'Refresh Services'
   menu option.  (Freemote Control will not have any bluetooth devices listed
   in the settings menu unless the serial port service is present for the
   paired bluetooth device.)

BUILD FROM SOURCE (Windows+Visual Studio)

   For the most part, the source will compile after creating a project
   and a config.h file with the following contents:

      #define HAVE_WIN32
      #define HAVE_VSTUDIO
      #define SYSCONFDIR .

   You will also need to add 'ws2_32.lib' to the Linker under
   Input->Additional Dependencies.

   I also turn off the precompiled headers in the Project's properties
   under the C/C++ section.

MAC OSX 10.6 PRECOMPILED BINARY

    Simply untar the tarball and execute the binary:

    i.e.:
       cd $HOME
       tar xvfz freemoted-x86_64-1.2-apple-darwin10.6.0.tar.gz
       cd freemoted
       ./freemoted

EXAMPLES

      If you wanted to control just the mouse and keyboard over wifi:

          freemoted -t socket

      If you want to control apps using LIRC:

          freemoted -t socket -m lirc -i localhost -j 8888

      If you want to control myth using the control port:

          freemoted -t socket -m myth -h localhost -p 6546

      Use -help option for usage.

KNOWN LIMITATIONS

    The myth control socket method alone is not recommended. When myth
    launches external programs such as mplayer, xine or xmame, the socket
    does not accept any commands until that program has exited.  This makes
    the remote useless while they are running.  The lirc and desktop methods
    are preferred.

    MythTV 0.22 introduced two bugs with the network control protocol
    preventing the 'Channels' and 'Recordings' layouts from functioning
    properly.   Patches have been submitted to the MythTV development team.
    If you are able to build your own MythTV binaries, patches are available
    at http://www.accentual.com/freemote

BUGS/ISSUES

    Separate Up/Down keystrokes don't seem to be working under Windows.

    On poor wifi connections, a long lag time between the keystroke down and
    up events can cause keys to be repeated (since it is equivalent to holding
    down a key on the keyboard).  If you find this is happening, use the
    settings window to turn off the 'Use separate key up/down events'.  (Note
    that you will lose the ability to hold down keys if you unselect this
    option.)

FILES

    freemoted.conf - maps buttons to lirc commands and/or keystrokes

CONTACT

    randy.rossi@gmail.com

