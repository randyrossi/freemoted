// winapp.cpp is the main entry point for the windows exe version of the Freemote Control server

#include "stdafx.h"
#include "winapp.h"
#include "shellapi.h"
#include <afxwin.h>
#include <stdio.h>

extern "C" {
	#include "config.h"
	#include "main.h"
	#include "threadutil.h"
}

#ifdef UNICODE
#define stringcopy wcscpy_s
#else
#define stringcopy strcpy
#endif

#define MAX_LOADSTRING 100

// Variabili globali:
HINSTANCE hInst;								// istanza corrente
TCHAR szTitle[MAX_LOADSTRING];					// Testo della barra del titolo
TCHAR szWindowClass[MAX_LOADSTRING];			// nome della classe di finestre principale


// Dichiarazioni con prototipo delle funzioni incluse in questo modulo di codice:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);

NOTIFYICONDATA g_notifyIconData ;
HWND g_hwnd ;
HMENU g_menu ;
UINT WM_TASKBARCREATED = 0 ;

#define ID_TRAY_APP_ICON                5000
#define ID_TRAY_OPEN_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3001
#define WM_TRAYICON ( WM_USER + 1 )

DWORD gPort = 8001;
DWORD gConnectionType = CT_SOCKET;
LPWSTR gConfigFile = NULL;
LPWSTR gIconFile = NULL;
LPWSTR gPortAsString = NULL;
HWND hPort = NULL;
HWND hSocket = NULL;
HWND hBluetooth = NULL;
HWND hConnectionType = NULL;
HWND hStatus = NULL;
HWND hButton = NULL;
int freemoteState = FREEMOTED_STATUS_STOPPED;

WNDPROC OldButtonProc;
WNDPROC OldSocketButtonProc;
WNDPROC OldBluetoothButtonProc;

pxthread_t mainThread = NULL;

void stateChangeCallback(int state, void* arg)
{
	int reason;
	freemoteState = state;
	if (hStatus != NULL) {
		switch (state) {
		case FREEMOTED_STATUS_STARTING:
			SetWindowText(hStatus,TEXT("Starting"));
			break;
		case FREEMOTED_STATUS_LISTENING:
			SetWindowText(hStatus,TEXT("Listening"));
			// Now we set button active, and relabel to STOP
			SetWindowText(hButton,TEXT("STOP"));
			EnableWindow(hPort,FALSE);
			EnableWindow(hSocket,FALSE);
			EnableWindow(hBluetooth,FALSE);
			EnableWindow(hButton,TRUE);
			break;
		case FREEMOTED_STATUS_STOPPING:
			SetWindowText(hStatus,TEXT("Stopping"));
			break;
		case FREEMOTED_STATUS_STOPPED:
			SetWindowText(hStatus,TEXT("Stopped"));
			// Now we set text field editable, button active, and relabel to START
			SetWindowText(hButton,TEXT("START"));
			EnableWindow(hPort,TRUE);
			EnableWindow(hButton,TRUE);
			EnableWindow(hSocket,TRUE);
			EnableWindow(hBluetooth,TRUE);
			break;
		case FREEMOTED_STATUS_FAILED:
			reason = (int) arg;
			SetWindowText(hStatus,TEXT("Failed"));
			if (reason == FREEMOTED_FAILURE_READCONFIG) {
				MessageBox(g_hwnd, TEXT("Could not read/find config file."), NULL, MB_APPLMODAL);
			} else if (reason == FREEMOTED_FAILURE_BIND) {
				MessageBox(g_hwnd, TEXT("Cannot bind port.  Port in use?"), NULL, MB_APPLMODAL);
			} else if (reason == FREEMOTED_FAILURE_BIND_BT) {
				MessageBox(g_hwnd, TEXT("Cannot bind bluetooth.  No device?"), NULL, MB_APPLMODAL);
			} else {
				MessageBox(g_hwnd, TEXT("Unkown Error"), NULL, MB_APPLMODAL);
			}
			break;
		default:
			break;
		}
	}
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	WSADATA wsaData;
	int iResult;

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Create space for gPortAsString
	gPortAsString = (LPWSTR) malloc(32);

	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);


	WM_TASKBARCREATED = RegisterWindowMessageA("TaskbarCreated") ;


 	// TODO: inserire qui il codice.
	MSG msg;
	HACCEL hAccelTable;

	//DWORD dwDisposition;
	HKEY hKey;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, KEY_ALL_ACCESS, &hKey)!=ERROR_SUCCESS)
	{
		// No key, create one
		//RegCreateKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, NULL, 0, 0, NULL, &hKey, &dwDisposition);

		//if (dwDisposition != REG_CREATED_NEW_KEY && dwDisposition != REG_OPENED_EXISTING_KEY) {
	        //return FALSE;
		//}

		//RegCloseKey(hKey);

		// Set default value
		//if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, KEY_ALL_ACCESS, &hKey)!=ERROR_SUCCESS) {
			//return FALSE;
		//}
		//else
		//{
			//if (RegSetValueEx(hKey, TEXT("Port"), NULL, REG_DWORD, (CONST BYTE*)&gPort, sizeof(DWORD))!=ERROR_SUCCESS) {
				//return FALSE;
			//}
		//}
		MessageBox(g_hwnd, TEXT("Required registry entries not found. Install problem?"), NULL, MB_APPLMODAL);
		return FALSE;
	}

	unsigned long type = 0,size = 0;
	unsigned int strLen = 0;

	// Key is open
	type = REG_DWORD;
	size = sizeof(DWORD);
	RegQueryValueEx(hKey,TEXT("Port"),NULL,&type,(LPBYTE)&gPort,&size);

	type = REG_DWORD;
	size = sizeof(DWORD);
	RegQueryValueEx(hKey,TEXT("ConnectionType"),NULL,&type,(LPBYTE)&gConnectionType,&size);

	type = REG_SZ;
	size = 0;
	RegQueryValueEx(hKey,TEXT("ConfigFile"),NULL,&type,NULL,&size);
	strLen = size + (1 * sizeof(TCHAR));
	gConfigFile = (LPWSTR) malloc(strLen);
	RegQueryValueEx(hKey, TEXT("ConfigFile"), 0, &type, (LPBYTE) gConfigFile, &size );

	type = REG_SZ;
	size = 0;
	RegQueryValueEx(hKey,TEXT("IconFile"),NULL,&type,NULL,&size);
	strLen = size + (1 * sizeof(TCHAR));
	gIconFile = (LPWSTR) malloc(strLen);
	RegQueryValueEx(hKey, TEXT("IconFile"), 0, &type, (LPBYTE) gIconFile, &size );

	RegCloseKey(hKey);
	f_setAcceptPort(gPort);
	f_setConnectionType(gConnectionType);
	f_setConfigFile((char*) gConfigFile);

	// Inizializzare le stringhe globali
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_FREEMOTECONTROL, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Eseguire l'inizializzazione dall'applicazione:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_FREEMOTECONTROL));

	// Ciclo di messaggi principale:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// Once you get the quit message, before exiting the app,
	// clean up and remove the tray icon
	if( !IsWindowVisible( g_hwnd ) )
	{
	    Shell_NotifyIcon(NIM_DELETE, &g_notifyIconData);
	}

	WSACleanup();

	return (int) msg.wParam;
}


LRESULT CALLBACK SocketButtonProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONUP:
		// What is the radio button set to?
		WORD tmpsk = GetWindowWord( hSocket, 0 );
		{
			gConnectionType = CT_SOCKET;
		}

		// Save it to registry & inform main
		HKEY hKey2;
		if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, KEY_ALL_ACCESS, &hKey2)==ERROR_SUCCESS) {
			RegSetValueEx(hKey2, TEXT("ConnectionType"), NULL, REG_DWORD, (CONST BYTE*)&gConnectionType, sizeof(DWORD));			
			RegCloseKey(hKey2);
		}
		f_setConnectionType(gConnectionType);

	}
	return CallWindowProc (OldSocketButtonProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK BluetoothButtonProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONUP:
		// What is the radio button set to?
		WORD tmpsk = GetWindowWord( hBluetooth, 0 );
		{
			gConnectionType = CT_BLUETOOTH;
		}

		// Save it to registry & inform main
		HKEY hKey2;
		if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, KEY_ALL_ACCESS, &hKey2)==ERROR_SUCCESS) {
			RegSetValueEx(hKey2, TEXT("ConnectionType"), NULL, REG_DWORD, (CONST BYTE*)&gConnectionType, sizeof(DWORD));			
			RegCloseKey(hKey2);
		}
		f_setConnectionType(gConnectionType);
	}
	return CallWindowProc (OldBluetoothButtonProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK ButtonProc (HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        return CallWindowProc (OldButtonProc, hwnd, msg, wp, lp);
    case WM_LBUTTONUP:

		// First check the port
		GetWindowText(hPort,gPortAsString,32);
		swscanf_s(gPortAsString,TEXT("%d"),&gPort);

		if (gPort < 1 || gPort > 65535) {
			MessageBox(g_hwnd, TEXT("Invalid port.  Valid range is 1-65535."), NULL, MB_APPLMODAL);
			return CallWindowProc (OldButtonProc, hwnd, msg, wp, lp);
		} else {
			HKEY hKey;
			if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, KEY_ALL_ACCESS, &hKey)==ERROR_SUCCESS) {
				RegSetValueEx(hKey, TEXT("Port"), NULL, REG_DWORD, (CONST BYTE*)&gPort, sizeof(DWORD));			
				RegCloseKey(hKey);
			}
			f_setAcceptPort(gPort);
		}

		// What is the radio button set to?
		WORD tmpbt = GetWindowWord( hBluetooth, 0 );
		WORD tmpsk = GetWindowWord( hSocket, 0 );
		if ((tmpbt & 0x01) == 0x01) {
			gConnectionType = CT_BLUETOOTH;
		} else if ((tmpsk & 0x01) == 0x01) {
			gConnectionType = CT_SOCKET;
		}

		// Save it to registry & inform main
		HKEY hKey2;
		if (RegOpenKeyEx(HKEY_CURRENT_USER, TEXT("Software\\Accentual\\Freemote"), 0, KEY_ALL_ACCESS, &hKey2)==ERROR_SUCCESS) {
			RegSetValueEx(hKey2, TEXT("ConnectionType"), NULL, REG_DWORD, (CONST BYTE*)&gConnectionType, sizeof(DWORD));			
			RegCloseKey(hKey2);
		}
		f_setConnectionType(gConnectionType);

		if (freemoteState == FREEMOTED_STATUS_STOPPED) {
			// Button goes inactive, text field goes non-editable
			EnableWindow(hButton,FALSE);
			EnableWindow(hPort,FALSE);
			EnableWindow(hSocket,FALSE);
			EnableWindow(hBluetooth,FALSE);
			mainThread = thread_create(f_startup, stateChangeCallback, (char*) "main");
			thread_run(mainThread);
		} else if (freemoteState == FREEMOTED_STATUS_LISTENING) {
			// Button goes inactive
			EnableWindow(hButton,FALSE);
			f_shutdown();
		}


        return CallWindowProc (OldButtonProc, hwnd, msg, wp, lp);
    }
    return CallWindowProc (OldButtonProc, hwnd, msg, wp, lp);
}


void Minimize()
{
  // add the icon to the system tray
  Shell_NotifyIcon(NIM_ADD, &g_notifyIconData);
  //Shell_NotifyIcon(NIM_SETFOCUS,&g_notifyIconData);

  // ..and hide the main window
  ShowWindow(g_hwnd, SW_HIDE);
}


void Restore()
{
  // Remove the icon from the system tray
  Shell_NotifyIcon(NIM_DELETE, &g_notifyIconData);

  // ..and show the window
  ShowWindow(g_hwnd, SW_SHOW);
}

void InitNotifyIconData()
{
  memset( &g_notifyIconData, 0, sizeof( NOTIFYICONDATA ) ) ;

  g_notifyIconData.cbSize = sizeof(NOTIFYICONDATA);

  g_notifyIconData.hWnd = g_hwnd;
  g_notifyIconData.uID = ID_TRAY_APP_ICON;

  /////
  // Set up flags.
  g_notifyIconData.uFlags = NIF_ICON | // promise that the hIcon member WILL BE A VALID ICON!!
    NIF_MESSAGE | // when someone clicks on the system tray icon,
    // we want a WM_ type message to be sent to our WNDPROC
    NIF_TIP;      // we're gonna provide a tooltip as well, son.

  g_notifyIconData.uCallbackMessage = WM_TRAYICON; //this message must be handled in hwnd's window procedure. more info below.

  // Load da icon.  Be sure to include an icon "green_man.ico" .. get one
  // from the internet if you don't have an icon
  g_notifyIconData.hIcon = (HICON)LoadImage( NULL, gIconFile, IMAGE_ICON, 0, 0, LR_LOADFROMFILE  ) ;

  // set the tooltip text.  must be LESS THAN 64 chars
  stringcopy(g_notifyIconData.szTip, TEXT("Freemote Control Server"));
 
  
}



ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_FREEMOTECONTROL));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_FREEMOTECONTROL);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; 

   g_hwnd = CreateWindowEx (

    0, szWindowClass,

    TEXT( "Freemote Control Server" ),
	WS_OVERLAPPEDWINDOW & ~(WS_SIZEBOX  | WS_MAXIMIZEBOX ) ,

    CW_USEDEFAULT, CW_USEDEFAULT,
    400, 200, 

    NULL, NULL,
    hInstance, NULL
  );

   if (!g_hwnd)
   {
      return FALSE;
   }

   CreateWindow( TEXT("static"), TEXT("Freemote Control Server"), WS_CHILD | WS_VISIBLE | SS_CENTER ,
                  0, 0, 400, 400, g_hwnd, 0, hInstance, NULL ) ;
   
   CreateWindow( TEXT("static"), TEXT("Listen Port:"), WS_CHILD | WS_VISIBLE | SS_LEFT ,
                  10, 50, 400, 20, g_hwnd, 0, hInstance, NULL ) ;

   wsprintf(gPortAsString,TEXT("%d"),gPort);

   hSocket = CreateWindowEx(0,TEXT("BUTTON"),TEXT("Wifi"),WS_GROUP | WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
	   10,20,50,20,g_hwnd,(HMENU)3004,hInstance,NULL);
    
   hBluetooth = CreateWindowEx(0,TEXT("BUTTON"),TEXT("Bluetooth"),WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
	   100,20,100,20,g_hwnd,(HMENU)3005,hInstance,NULL);

   WORD tmp_chk = GetWindowWord( hSocket, 0 );
   if ( gConnectionType == CT_SOCKET )
	   tmp_chk |= 0x0001;
   else
	   tmp_chk &= ~0x0001;
   SetWindowWord( hSocket, 0, tmp_chk);

   tmp_chk = GetWindowWord( hBluetooth, 0 );
   if ( gConnectionType == CT_BLUETOOTH )
	   tmp_chk |= 0x0001;
   else
	   tmp_chk &= ~0x0001;
   SetWindowWord( hBluetooth, 0, tmp_chk );


   OldSocketButtonProc = (WNDPROC) SetWindowLong (hSocket, GWL_WNDPROC, (LONG) SocketButtonProc);
   OldBluetoothButtonProc = (WNDPROC) SetWindowLong (hBluetooth, GWL_WNDPROC, (LONG) BluetoothButtonProc);

   hPort = CreateWindow(TEXT("EDIT"), gPortAsString,WS_TABSTOP | WS_GROUP | WS_CHILD|WS_VISIBLE|ES_NUMBER,
				100,50,100,20,g_hwnd,(HMENU)3002,hInstance,NULL);

   CreateWindow( TEXT("static"), TEXT("Status:"), WS_CHILD | WS_VISIBLE | SS_LEFT,
                  10, 90, 400, 20, g_hwnd, 0, hInstance, NULL ) ;

   hStatus = CreateWindow( TEXT("static"), TEXT("Initializing"), WS_CHILD | WS_VISIBLE | SS_LEFT,
                  100, 90, 300, 20, g_hwnd, 0, hInstance, NULL ) ;
   
   hButton = CreateWindow(TEXT("button"),TEXT(""),WS_CHILD|SS_LEFT,140,110,120,30,g_hwnd,0,hInstance,NULL);
   OldButtonProc = (WNDPROC) SetWindowLong (hButton, GWL_WNDPROC, (LONG) ButtonProc);


   ShowWindow(hButton,SW_SHOW);

   InitNotifyIconData();

   ShowWindow(g_hwnd, nCmdShow);
   UpdateWindow(g_hwnd);

   return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	if ( message==WM_TASKBARCREATED && !IsWindowVisible( g_hwnd ) )
	{
	    Minimize();
	    return 0;
	}

	switch (message)
	{
	case WM_CREATE:
		g_menu = CreatePopupMenu();
		AppendMenu(g_menu, MF_STRING, ID_TRAY_OPEN_CONTEXT_MENU_ITEM,  TEXT( "Open Freemote Control Server" ) );
		AppendMenu(g_menu, MFT_SEPARATOR, NULL, NULL);
		AppendMenu(g_menu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM,  TEXT( "Exit" ) );

		// Create and start freemoted thread...
		mainThread = thread_create(f_startup, stateChangeCallback, (char*) "main");
		thread_run(mainThread);

		return 0;
	case WM_SYSCOMMAND:
	    switch( wParam & 0xfff0 ) 
		{
		case SC_MINIMIZE:
		case SC_CLOSE:  // redundant to WM_CLOSE, it appears
			Minimize() ;
			return 0 ;
		}
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);

		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			return 0;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			return 0;
		}
		break;		
	case WM_TRAYICON:
    {
      switch(wParam)
      {
		case ID_TRAY_APP_ICON:
			break;
      }

      // the mouse button has been released.

      // I'd LIKE TO do this on WM_LBUTTONDOWN, it makes
      // for a more responsive-feeling app but actually
      // the guy who made the original post is right.
      // Most apps DO respond to WM_LBUTTONUP, so if you
      // restore your window on WM_LBUTTONDOWN, then some
      // other icon will scroll in under your mouse so when
      // the user releases the mouse, THAT OTHER ICON will
      // get the WM_LBUTTONUP command and that's quite annoying.
      if (lParam == WM_LBUTTONUP)
      {
        Restore();
		return 0;
      }
      else if (lParam == WM_RBUTTONDOWN) // I'm using WM_RBUTTONDOWN here because
      {
        // it gives the app a more responsive feel.  Some apps
        // DO use this trick as well.  Right clicks won't make
        // the icon disappear, so you don't get any annoying behavior
        // with this (try it out!)

        // Get current mouse position.
        POINT curPoint ;
        GetCursorPos( &curPoint ) ;

        // should SetForegroundWindow according
        // to original poster so the popup shows on top
        SetForegroundWindow(hWnd); 

        // TrackPopupMenu blocks the app until TrackPopupMenu returns
        UINT clicked = TrackPopupMenu(

          g_menu,
          TPM_RETURNCMD | TPM_NONOTIFY, // don't send me WM_COMMAND messages about this window, instead return the identifier of the clicked menu item
          curPoint.x,
          curPoint.y,
          0,
          hWnd,
          NULL

        );

        // Original poster's line of code.  Haven't deleted it,
        // but haven't seen a need for it.
        //SendMessage(hWnd, WM_NULL, 0, 0); // send benign message to window to make sure the menu goes away.
        if (clicked == ID_TRAY_EXIT_CONTEXT_MENU_ITEM)
        {
          // quit the application.
          PostQuitMessage( 0 ) ;
		  return 0;
		}
		else if (clicked == ID_TRAY_OPEN_CONTEXT_MENU_ITEM)
		{
		  Restore();
		  return 0;
		}
      }
    }
    break;
	
	// intercept the hittest message.. making full body of
	// window draggable.
	case WM_NCHITTEST:
	{
	    // http://www.catch22.net/tuts/tips
	    // this tests if you're on the non client area hit test
	    UINT uHitTest = DefWindowProc(hWnd, WM_NCHITTEST, wParam, lParam);
	    if(uHitTest == HTCLIENT)
			return HTCAPTION;
		else
			return uHitTest;
	}
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);
		// TODO: aggiungere qui il codice per il disegno...
		EndPaint(hWnd, &ps);
		return 0;
		break;
	case WM_CLOSE:
		Minimize() ;
		return 0;
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
		break;
	
	default:
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Gestore dei messaggi della finestra Informazioni su.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}


