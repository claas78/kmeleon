/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *   Chak Nanga <chak@netscape.com> 
 */

// File Overview....
//
// The typical MFC View, toolbar, statusbar creation done 
// in CBrowserFrame::OnCreate()
//
// Code to update the Status/Tool bars in response to the
// Web page loading progress(called from methods in CBrowserImpl)
//
// SetupFrameChrome() determines what, if any, UI elements this Frame
// will sport based on the current "chromeMask" 
//
// Also take a look at OnClose() which gets used when you close a browser
// window. This needs to be overrided mainly to handle supporting multiple
// browser frame windows via the "New Browser Window" menu item
// Without this being overridden the MFC framework handles the OnClose and
// shutsdown the complete application when a frame window is closed.
// In our case, we want the app to shutdown when the File/Exit menu is chosen
//
// Another key functionality this object implements is the IBrowserFrameGlue
// interface - that's the interface the Gecko embedding interfaces call
// upong to update the status bar etc.
// (Take a look at IBrowserFrameGlue.h for the interface definition and
// the BrowserFrm.h to see how we implement this interface - as a nested
// class)
// We pass this Glue object pointer to the CBrowserView object via the 
// SetBrowserFrameGlue() method. The CBrowserView passes this on to the
// embedding interface implementaion
//
// Please note the use of the macro METHOD_PROLOGUE in the implementation
// of the nested BrowserFrameGlue object. Essentially what this macro does
// is to get you access to the outer (or the object which is containing the
// nested object) object via the pThis pointer.
// Refer to the AFXDISP.H file in VC++ include dirs
//
// Next suggested file to look at : BrowserView.cpp

#include "stdafx.h"

#include "MfcEmbed.h"
extern CMfcEmbedApp theApp;

#include "BrowserFrm.h"

#include "ToolBarEx.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CBrowserFrame

IMPLEMENT_DYNAMIC(CBrowserFrame, CFrameWnd)

BEGIN_MESSAGE_MAP(CBrowserFrame, CFrameWnd)
	//{{AFX_MSG_MAP(CBrowserFrame)
	ON_WM_CREATE()
	ON_WM_SETFOCUS()
	ON_WM_SIZE()
	ON_WM_CLOSE()
  ON_WM_SYSCOLORCHANGE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

static UINT indicators[] =
{
	ID_SEPARATOR,            // For the Status line
	ID_PROG_BAR            // For the Progress Bar
};

#define BAND_BASE_ID 200

/////////////////////////////////////////////////////////////////////////////
// CBrowserFrame construction/destruction

CBrowserFrame::CBrowserFrame(PRUint32 chromeMask)
{
	// Save the chromeMask off. It'll be used
	// later to determine whether this browser frame
	// will have menubar, toolbar, statusbar etc.

	m_chromeMask = chromeMask;
}

CBrowserFrame::~CBrowserFrame()
{
}

void CBrowserFrame::OnClose()
{
  // if we don't do this, then our menu will be destroyed when the first window is.
  // that's bad because our menu is shared between all windows
  SetMenu(NULL);

  SaveBandSizes();

	CMfcEmbedApp *pApp = (CMfcEmbedApp *)AfxGetApp();
	pApp->RemoveFrameFromList(this);

	DestroyWindow();
}

// This is where the UrlBar, ToolBar, StatusBar, ProgressBar
// get created
// 
int CBrowserFrame::OnCreate(LPCREATESTRUCT lpCreateStruct){
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

  // tell all our plugins that we were created
  theApp.plugins.OnCreate(this->m_hWnd);

	// Pass "this" to the View for later callbacks
	// and/or access to any public data members, if needed
	//
	m_wndBrowserView.SetBrowserFrame(this);

	// Pass on the BrowserFrameGlue also to the View which
	// it will use during the Init() process after creation
	// of the BrowserImpl obj. Essentially, the View object
	// hooks up the Embedded browser's callbacks to the BrowserFrame
	// via this BrowserFrameGlue object
	m_wndBrowserView.SetBrowserFrameGlue((PBROWSERFRAMEGLUE)&m_xBrowserFrameGlueObj);

	// create a view to occupy the client area of the frame
	// This will be the view in which the embedded browser will
	// be displayed in
	//
	if (!m_wndBrowserView.Create(NULL, NULL, AFX_WS_DEFAULT_VIEW,
		CRect(0, 0, 0, 0), this, AFX_IDW_PANE_FIRST, NULL))
	{
		TRACE0("Failed to create view window\n");
		return -1;
	}

	// create the URL bar (essentially a ComboBoxEx object)
	if (!m_wndUrlBar.Create(CBS_DROPDOWN | WS_CHILD, CRect(0, 0, 200, 150), this, ID_URL_BAR))
	{
		TRACE0("Failed to create URL Bar\n");
		return -1;      // fail to create
	}

	// Create the toolbar with Back, Fwd, Stop, etc. buttons..
	if (!m_wndToolBar.CreateEx (this, 0x40 | /*CCS_NOPARENTALIGN | CCS_NORESIZE | */ TBSTYLE_FLAT | TBSTYLE_TRANSPARENT | TBSTYLE_AUTOSIZE | TBSTYLE_TOOLTIPS) )
  {
		TRACE0("Failed to create toolbar\n");
		return -1;      // fail to create
	}
  // back, forward, stop, reload, home, search
  UINT buttons[6] = {
    ID_NAV_BACK,
    ID_NAV_FORWARD,
    ID_NAV_STOP,
    ID_NAV_RELOAD,
    ID_NAV_HOME,
    ID_NAV_SEARCH
  };

  m_toolbarColdImageList.Create(IDB_TOOLBAR_COLD, 16, 3, RGB(255, 0, 255));
  m_wndToolBar.GetToolBarCtrl().SetImageList(&m_toolbarColdImageList);

  m_toolbarHotImageList.Create(IDB_TOOLBAR_HOT, 16, 8, RGB(255, 0, 255));
  m_wndToolBar.GetToolBarCtrl().SetHotImageList(&m_toolbarHotImageList);
/*
  m_toolbarDisabledImageList.Create(IDB_TOOLBAR_DISABLED, 16, 8, RGB(255, 0, 255));
  m_wndToolBar.GetToolBarCtrl().SetDisabledImageList(&m_toolbarDisabledImageList);
*/
  m_wndToolBar.SetButtons(buttons, 6);

  m_wndUrlBar.SetImageList(&m_toolbarHotImageList);


  // Create the animation control..
	if (!m_wndAnimate.Create(WS_CHILD | WS_VISIBLE, CRect(0, 0, 10, 10), this, AFX_IDW_TOOLBAR + 2) ||
    !m_wndAnimate.Open(IDR_MFCAVI))
	{
		TRACE0("Failed to create aimation\n");
		return -1;      // fail to create
	}

	// Create a ReBar window to which the toolbar and UrlBar 
	// will be added
	if (!m_wndReBar.Create(this))
	{
		TRACE0("Failed to create ReBar\n");
		return -1;      // fail to create
	}

	//Add the ToolBar and UrlBar windows to the rebar
	m_wndReBar.AddBar(&m_wndToolBar, NULL);
	m_wndReBar.AddBar(&m_wndUrlBar, "URL:");
  m_wndReBar.AddBar(&m_wndAnimate, NULL, NULL, RBBS_FIXEDSIZE | RBBS_FIXEDBMP);

  //--------------------------------------------------------------
	// Set up min/max sizes and ideal sizes for pieces of the rebar:
	//--------------------------------------------------------------
  CReBarCtrl *rebarControl = &m_wndReBar.GetReBarCtrl();
  
	REBARBANDINFO rbbi;

  // Address Bar
	CRect rectAddress;
	m_wndUrlBar.GetEditCtrl()->GetWindowRect(&rectAddress);

	rbbi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_SIZE;;
  rbbi.cxMinChild = 0;
	rbbi.cyMinChild = rectAddress.Height() + 7;
	rbbi.cxIdeal = 200;
  rbbi.cx = 200;
	rebarControl->SetBandInfo (1, &rbbi);

  rebarControl->MaximizeBand(1);

  // Toolbar
	CRect rectToolBar;
	m_wndToolBar.GetItemRect(0, &rectToolBar);

	rbbi.cbSize = sizeof(rbbi);
	rbbi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_SIZE;
	rbbi.cxMinChild = 0;
	rbbi.cyMinChild = rectToolBar.Height();
	rbbi.cxIdeal = rectToolBar.Width() * m_wndToolBar.GetCount();
  rbbi.cx = rbbi.cxIdeal + 10;
	rebarControl->SetBandInfo (0, &rbbi);

  theApp.plugins.DoRebars(m_wndReBar.GetReBarCtrl().m_hWnd);

  RestoreBandSizes();

  LoadBackImage();
  SetBackImage();

	// Create the status bar with two panes - one pane for actual status
	// text msgs. and the other for the progress control
	if (!m_wndStatusBar.Create(this) ||
		!m_wndStatusBar.SetIndicators(indicators, sizeof(indicators)/sizeof(UINT)))
	{
		TRACE0("Failed to create status bar\n");
		return -1;      // fail to create
	}
  m_wndStatusBar.SetPaneStyle(m_wndStatusBar.CommandToIndex(ID_SEPARATOR), SBPS_STRETCH);

	// Create the progress bar as a child of the status bar.
	// Note that the ItemRect which we'll get at this stage
	// is bogus since the status bar panes are not fully
	// positioned yet i.e. we'll be passing in an invalid rect
	// to the Create function below
	// The actual positioning of the progress bar will be done
	// in response to OnSize()
	RECT rc;
	m_wndStatusBar.GetItemRect (m_wndStatusBar.CommandToIndex(ID_PROG_BAR), &rc);
	if (!m_wndProgressBar.Create(WS_CHILD|WS_VISIBLE|PBS_SMOOTH, rc, &m_wndStatusBar, ID_PROG_BAR))
	{
		TRACE0("Failed to create progress bar\n");
		return -1;      // fail to create
	}

	// Based on the "chromeMask" we were supplied during construction
	// hide any requested UI elements - statusbar, menubar etc...
	// Note that the window styles (WM_RESIZE etc) are set inside
	// of PreCreateWindow()

	SetupFrameChrome(); 

  m_wndUrlBar.SetFocus();

	return 0;
}

void CBrowserFrame::SaveBandSizes(){
  REBARBANDINFO rbbi;
  rbbi.cbSize = sizeof(rbbi);
  rbbi.fMask = RBBIM_SIZE | RBBIM_ID | RBBIM_STYLE;

  CReBarCtrl *rebarControl = &m_wndReBar.GetReBarCtrl();
  int bandCount = rebarControl->GetBandCount();
  int i;
  char tempPref[256] = _T("kmeleon.toolband"); // 16 chars
  for ( i=0; i < bandCount; i++){
    rebarControl->GetBandInfo(i, &rbbi);

    if (rbbi.wID){
      sprintf(tempPref + 16, _T("%i.size"), rbbi.wID);
      theApp.preferences.SetInt(tempPref, rbbi.cx);

      sprintf(tempPref + 16, _T("%i.ndx"), rbbi.wID);
      theApp.preferences.SetInt(tempPref, i);

      sprintf(tempPref + 16, _T("%i.style"), rbbi.wID);
      theApp.preferences.SetInt(tempPref, rbbi.fStyle);
    }
  }
}

void CBrowserFrame::RestoreBandSizes(){
  REBARBANDINFO rbbi;
  rbbi.cbSize = sizeof(rbbi);

  CReBarCtrl *rebarControl = &m_wndReBar.GetReBarCtrl();
  int bandCount = rebarControl->GetBandCount();
  int i;
  for ( i=0; i < bandCount; i++){
    rbbi.fMask = RBBIM_ID;
    rbbi.wID = BAND_BASE_ID + i;
    rebarControl->SetBandInfo(i, &rbbi);
  }
  char tempPref[256] = _T("kmeleon.toolband"); // 16 chars
  for ( i=0; i < bandCount; i++){
    rbbi.fMask = 0;
    rbbi.wID = BAND_BASE_ID + i;

    sprintf(tempPref + 16, _T("%i.size"), rbbi.wID);
    rbbi.cx = theApp.preferences.GetInt(tempPref, 0);
    if (rbbi.cx > 0)
      rbbi.fMask |= RBBIM_SIZE;

    sprintf(tempPref + 16, _T("%i.style"), rbbi.wID);
    rbbi.fStyle = theApp.preferences.GetInt(tempPref, 0);
    if (rbbi.fStyle > 0)
      rbbi.fMask |= RBBIM_STYLE;

    sprintf(tempPref + 16, _T("%i.ndx"), rbbi.wID);
    int ndx = theApp.preferences.GetInt(tempPref, -1);
    if (ndx >= 0){
      // find us
      int current;
      for (current = 0; current < bandCount; current++){
        REBARBANDINFO rbsearch;
        rbsearch.cbSize = sizeof(rbbi);
        rbsearch.fMask = RBBIM_ID;
        rebarControl->GetBandInfo(current, &rbsearch);
        if (rbsearch.wID == rbbi.wID){
          rebarControl->MoveBand(current, ndx);
          rebarControl->SetBandInfo(ndx, &rbbi);
          break;
        }
      }
    }
  }
}

void CBrowserFrame::SetupFrameChrome()
{
  if(m_chromeMask == nsIWebBrowserChrome::CHROME_ALL){
		return;
  }

	if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_MENUBAR) )
		SetMenu(NULL); // Hide the MenuBar

	if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_TOOLBAR) )
		m_wndReBar.ShowWindow(SW_HIDE); // Hide the ToolBar

	if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_STATUSBAR) )
		m_wndStatusBar.ShowWindow(SW_HIDE); // Hide the StatusBar
}

BOOL CBrowserFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if( !CFrameWnd::PreCreateWindow(cs) )
		return FALSE;

	cs.dwExStyle &= ~WS_EX_CLIENTEDGE;

	// Change window style based on the chromeMask

	if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_TITLEBAR) )
		cs.style &= ~WS_CAPTION; // No caption		

	if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_WINDOW_RESIZE) )
	{
		// Can't resize this window
		cs.style &= ~WS_SIZEBOX;
		cs.style &= ~WS_THICKFRAME;
		cs.style &= ~WS_MINIMIZEBOX;
		cs.style &= ~WS_MAXIMIZEBOX;
	}

	cs.lpszClass = AfxRegisterWndClass(0);

  // this function is actually called twice per window.
  // the first time hInstance is 0
  if (cs.hInstance){
    CMenu *menu = theApp.menus.GetMenu(_T("Main"));
    if (menu){
      cs.hMenu = menu->m_hMenu;
    }
  }

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CBrowserFrame message handlers
void CBrowserFrame::OnSetFocus(CWnd* pOldWnd)
{
	// forward focus to the browser window
	m_wndBrowserView.mBaseWindow->SetFocus();
}

/*
LRESULT CBrowserFrame::WindowProc( UINT message, WPARAM wParam, LPARAM lParam ){
  LRESULT result = theApp.plugins.OnMessage( this->m_hWnd, message, wParam, lParam );

  LRESULT result2 = CFrameWnd::WindowProc( message, wParam, lParam );

  if (!result)
    return result2;
  else
    return result;
}
*/

BOOL CBrowserFrame::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
  if (nCode == CN_COMMAND){
    if (pHandlerInfo && theApp.plugins.OnUpdate(nID)){
      return true;
    }
  }

	// let the view have first crack at the command
	if (m_wndBrowserView.OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
		return TRUE;

	// otherwise, do default handling
	return CFrameWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

// Needed to properly position/resize the progress bar
//
void CBrowserFrame::OnSize(UINT nType, int cx, int cy) {
  CFrameWnd::OnSize(nType, cx, cy);

  // Get the ItemRect of the status bar's Pane 0
  // That's where the progress bar will be located
  RECT rc;
  m_wndStatusBar.GetItemRect(m_wndStatusBar.CommandToIndex(ID_PROG_BAR), &rc);

  // Move the progress bar into it's correct location
  //
  m_wndProgressBar.MoveWindow(&rc);

  if (nType == SIZE_MAXIMIZED){
    theApp.preferences.bMaximized = true;
  }else if (nType == SIZE_RESTORED){
    theApp.preferences.bMaximized = false;
  }
}

/////////////////////////////////////////////////////////////////////////////
void CBrowserFrame::OnSysColorChange() 
{
	CFrameWnd::OnSysColorChange();
	
	//-------------------------
	// Reload background image:
	//-------------------------
	LoadBackImage ();
	SetBackImage ();
}

/////////////////////////////////////////////////////////////////////////////
void CBrowserFrame::SetBackImage ()
{
	CReBarCtrl& rc = m_wndReBar.GetReBarCtrl ();

	for (UINT i = 0; i < rc.GetBandCount(); i++)
	{
		REBARBANDINFO info;
		memset (&info, 0, sizeof (REBARBANDINFO));
		info.cbSize = sizeof (info);
		info.fMask = RBBIM_BACKGROUND;
		info.hbmBack = theApp.preferences.bToolbarBackground ? (HBITMAP)m_bmpBack : NULL;
		rc.SetBandInfo (i, &info);

		CRect rectBand;
		rc.GetRect (i, rectBand);

		rc.InvalidateRect (rectBand);
		rc.UpdateWindow ();

		info.fMask = RBBIM_CHILD;
		rc.GetBandInfo (i, &info);

		if (info.hwndChild != NULL)
		{
			::InvalidateRect (info.hwndChild, NULL, TRUE);
			::UpdateWindow (info.hwndChild);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
void CBrowserFrame::LoadBackImage ()
{
	//------------------------------------
	// Load control bars background image:
	//------------------------------------

	if (m_bmpBack.GetSafeHandle () != NULL)
	{
		m_bmpBack.DeleteObject ();
	}

  HBITMAP hbmp;
  if (theApp.preferences.toolbarBackground.IsEmpty()){
    hbmp = (HBITMAP) ::LoadImage (AfxGetResourceHandle (),
			MAKEINTRESOURCE (IDB_BACK),
			IMAGE_BITMAP,
			0, 0,
			LR_LOADMAP3DCOLORS | LR_LOADTRANSPARENT);
	}else{
    hbmp = (HBITMAP) ::LoadImage (AfxGetResourceHandle (),
			theApp.preferences.toolbarBackground,
			IMAGE_BITMAP,
			0, 0,
			LR_LOADMAP3DCOLORS | LR_LOADFROMFILE);
	}

	m_bmpBack.Attach (hbmp);
}

#ifdef _DEBUG
void CBrowserFrame::AssertValid() const
{
	CFrameWnd::AssertValid();
}

void CBrowserFrame::Dump(CDumpContext& dc) const
{
	CFrameWnd::Dump(dc);
}

#endif //_DEBUG
