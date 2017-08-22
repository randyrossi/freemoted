// This file is part of Freemote Control Proxy.
//
// Freemote Control Proxy is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Freemote Control Proxy is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Freemote Control Proxy.  If not, see
// <http://www.gnu.org/licenses/>.

#import <Cocoa/Cocoa.h>

int g_argc;
char** g_argv;

extern int run(int argc, char **argv);

int main(int argc, char *argv[])
{
	g_argc = argc;
	g_argv = argv;
	return run(argc,argv);
	//return NSApplicationMain(argc,  (const char **) argv);
}
