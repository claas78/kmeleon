/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: Mozilla-sample-code 1.0
 *
 * Copyright (c) 2002 Netscape Communications Corporation and
 * other contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this Mozilla sample software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Contributor(s):
 *   Chak Nanga <chak@netscape.com> 
 *
 * ***** END LICENSE BLOCK ***** */

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

#include "BrowserFrm.h"
#include "BrowserView.h"
#include "ToolBarEx.h"
#include "KmeleonConst.h"

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
    ON_WM_MOVE()
    ON_WM_CLOSE()
    ON_WM_ACTIVATE()
    ON_WM_SYSCOLORCHANGE()
    ON_MESSAGE(UWM_REFRESHTOOLBARITEM, RefreshToolBarItem)
    ON_MESSAGE(UWM_REFRESHMRULIST, RefreshMRUList)
	ON_COMMAND(ID_CLOSE, CloseNothing)
    ON_COMMAND_RANGE(TOOLBAR_MENU_START_ID, TOOLBAR_MENU_END_ID, ToggleToolBar)
    ON_COMMAND(ID_TOOLBARS_LOCK, ToggleToolbarLock)
    ON_UPDATE_COMMAND_UI(ID_TOOLBARS_LOCK, OnUpdateToggleToolbarLock)
	ON_UPDATE_COMMAND_UI_RANGE(TOOLBAR_MENU_START_ID, TOOLBAR_MENU_END_ID, OnUpdateToolBarMenu)
	ON_COMMAND(ID_EDIT_FIND, OnShowFindBar)
    //}}AFX_MSG_MAP
	ON_WM_SYSCOMMAND()
END_MESSAGE_MAP()

static UINT indicators[] =
{
    ID_SEPARATOR,              // For the Status line
    ID_PROG_BAR,               // For the Progress Bar
    ID_SEPARATOR
};

#define PREF_TOOLBAND_LOCKED "kmeleon.general.toolbars_locked"

/////////////////////////////////////////////////////////////////////////////
// CBrowserFrame construction/destruction

CBrowserFrame::CBrowserFrame(PRUint32 chromeMask, LONG style = 0)
{
    // Save the chromeMask off. It'll be used
    // later to determine whether this browser frame
    // will have menubar, toolbar, statusbar etc.

    m_chromeMask = chromeMask;
    m_style = style;
    m_created = FALSE;
    m_ignoreFocus = 2;
    m_hSecurityIcon = NULL;
	m_wndFindBar = NULL;
}

CBrowserFrame::~CBrowserFrame()
{
    if (m_hSecurityIcon)
        DestroyIcon(m_hSecurityIcon);
	if (m_wndFindBar)
		delete m_wndFindBar;
}

BOOL CBrowserFrame::PreTranslateMessage(MSG* pMsg)
{
   if (pMsg->message==WM_KEYDOWN)
   {
	   if ( pMsg->wParam == VK_TAB) {
		nsCOMPtr<nsIWebBrowserFocus> focus(do_GetInterface(m_wndBrowserView.mWebBrowser));
		if(focus) {
				if (pMsg->hwnd == m_wndUrlBar.m_hwndEdit) {
				if (GetKeyState(VK_SHIFT)  & 0x8000)
					if (m_wndFindBar) 
						m_wndFindBar->SetFocus();
					else {
						focus->Activate();
						focus->SetFocusAtLastElement();
					}
				else {
					focus->Activate();
					focus->SetFocusAtFirstElement();
				}
				return 1;
			}
			else if ( m_wndFindBar && (pMsg->hwnd == m_wndFindBar->m_cEdit.m_hWnd)) { 
				
				if (GetKeyState(VK_SHIFT)  & 0x8000) {
					focus->Activate();
					focus->SetFocusAtLastElement();
				}
				else
					::SetFocus(m_wndUrlBar.m_hwndEdit);
				return 1;
			}
		}
	  }
   }  
   return CFrameWnd::PreTranslateMessage(pMsg);
}

void CBrowserFrame::OnClose()
{
   DWORD dwWaitResult; 
   dwWaitResult = WaitForSingleObject( theApp.m_hMutex, 1000L);
   if (dwWaitResult != WAIT_OBJECT_0) {
     return;
   }

   // tell all our plugins that we are closing
   theApp.plugins.SendMessage("*", "* OnClose", "Close", (long)m_hWnd);

   // if we don't do this, then our menu will be destroyed when the first window is.
   // that's bad because our menu is shared between all windows
   SetMenu(NULL);

   // Make sure we don't leave screen residue
   if (m_wndBrowserView.mWebNav)
      m_wndBrowserView.mWebNav->Stop(nsIWebNavigation::STOP_ALL);

   // the browserframeglue will be deleted soon, so we set it to null so it won't try to access it after it's deleted.
   m_wndBrowserView.SetBrowserFrameGlue(NULL);

   if (theApp.m_pMostRecentBrowserFrame == this) {
      CBrowserFrame* pFrame;

      POSITION pos = theApp.m_FrameWndLst.Find(this);
      theApp.m_FrameWndLst.GetPrev(pos);
      if (pos) pFrame = (CBrowserFrame *) theApp.m_FrameWndLst.GetPrev(pos);  // previous frame
      else     pFrame = (CBrowserFrame *) theApp.m_FrameWndLst.GetTail();     // last frame

      if (pFrame != this)  theApp.m_pMostRecentBrowserFrame = pFrame;
      else                 theApp.m_pMostRecentBrowserFrame = NULL;
      // if no other browser views exist, nullify the pointer

      if (!theApp.m_pMostRecentBrowserFrame && !(m_style & WS_POPUP)){
          m_wndReBar.SaveBandSizes();
      }
   }

    WINDOWPLACEMENT wp;
    wp.length = sizeof (WINDOWPLACEMENT);
    GetWindowPlacement(&wp);

    // only record the window size for non-popup windows
    if (!(m_style & WS_POPUP)){
        // record the maximized state
        if (wp.showCmd == SW_SHOWMAXIMIZED)
            theApp.preferences.bMaximized = true;
        // record the window size/pos
        else if (wp.showCmd == SW_SHOWNORMAL) {
            theApp.preferences.bMaximized = false;

            RECT rc; // = wp.rcNormalPosition;
            GetWindowRect(&rc);
            if (rc.left >= 0 && rc.top >= 0 ) {
                theApp.preferences.windowWidth = rc.right - rc.left;
                theApp.preferences.windowHeight = rc.bottom - rc.top;
                theApp.preferences.windowXPos = rc.left;
                theApp.preferences.windowYPos = rc.top;
            }
        }
    }

   // tell all our plugins that we are closing
   theApp.plugins.SendMessage("*", "* OnClose", "Destroy", (long)m_hWnd);

   DestroyWindow();
   theApp.RemoveFrameFromList(this);
}






/*

  This is identical to the MFC CFrameWnd::Create function
  except that the rect structure passed to it contains
  x, y, cx, cx values instead of left, top, right, bottom
  and the menu is loaded differently
  
*/


BOOL CBrowserFrame::Create(LPCTSTR lpszClassName,
                           LPCTSTR lpszWindowName,
                           DWORD dwStyle,
                           const RECT& rect,
                           CWnd* pParentWnd,
                           LPCTSTR lpszMenuName,
                           DWORD dwExStyle,
                           CCreateContext* pContext)
{
   m_hMenu = NULL;
   CMenu *menu = theApp.menus.GetMenu(_T("Main"));
   if (menu){
      m_hMenu = menu->m_hMenu;
   }
   
   m_strTitle = lpszWindowName;    // save title for later
   
   if (!CreateEx(dwExStyle, lpszClassName, lpszWindowName, dwStyle,
      rect.left, rect.top, rect.right, rect.bottom,
      pParentWnd->GetSafeHwnd(), m_hMenu, (LPVOID)pContext))
   {
      TRACE0("Warning: failed to create CFrameWnd.\n");
      if (m_hMenu != NULL)
         DestroyMenu(m_hMenu);
      m_hMenu = NULL;
       ReleaseMutex(theApp.m_hMutex);
      return FALSE;
   }

   theApp.menus.SetCheck(ID_OFFLINE, theApp.preferences.bOffline);
       
       ReleaseMutex(theApp.m_hMutex);
   return TRUE;
}

// This is where the UrlBar, ToolBar, StatusBar, ProgressBar
// get created
// 
int CBrowserFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
        return -1;

    // tell all our plugins that we were created
    theApp.plugins.SendMessage("*", "* OnCreate", "Create", (long)this->m_hWnd, (long)lpCreateStruct);

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

    // Load the Most Recently Used(MRU) Urls into the UrlBar
    m_wndUrlBar.LoadMRUList();  
//   m_wndUrlBar.SetImageList(&m_toolbarHotImageList);

    BOOL bThrobber = TRUE;
    // Create the animation control..
    if (!m_wndAnimate.Create(WS_CHILD | WS_VISIBLE, CRect(0, 0, 10, 10), this, ID_THROBBER))
    {
        TRACE0("Failed to create animation\n");
        bThrobber = FALSE;
    }

	CString throbberPath;
	theApp.FindSkinFile(throbberPath, _T("Throbber.avi"));
	if (!m_wndAnimate.Open(throbberPath))
		bThrobber = FALSE;

    // Create a ReBar window to which the toolbar and UrlBar 
    // will be added
    if (!m_wndReBar.Create(this, RBS_DBLCLKTOGGLE | RBS_VARHEIGHT | RBS_BANDBORDERS))
    {
        TRACE0("Failed to create ReBar\n");
        return -1;      // fail to create
    }

    //Add the UrlBar and Throbber windows to the rebar
    char szTitle[256] = "URL:";
    theApp.preferences.GetString("kmeleon.display.URLbarTitle", (char*)&szTitle, (char*)&szTitle);
    m_wndReBar.AddBar(&m_wndUrlBar, szTitle);
    if (bThrobber)
        m_wndReBar.AddBar(&m_wndAnimate, NULL, NULL, RBBS_FIXEDSIZE | RBBS_FIXEDBMP);

    m_wndReBar.RegisterBand(m_wndUrlBar.m_hWnd,  "URL Bar", true);
    if (bThrobber)
        m_wndReBar.RegisterBand(m_wndAnimate.m_hWnd, "Throbber", false);

    //--------------------------------------------------------------
    // Set up min/max sizes and ideal sizes for pieces of the rebar:
    //--------------------------------------------------------------
    CReBarCtrl *rebarControl = &m_wndReBar.GetReBarCtrl();
  
    // Address Bar
    CRect rectAddress;
    m_wndUrlBar.GetEditCtrl()->GetWindowRect(&rectAddress);

    REBARBANDINFO rbbi;
    rbbi.cbSize = sizeof(rbbi);
    rbbi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_SIZE;
    rbbi.cxMinChild = 0;
    rbbi.cyMinChild = rectAddress.Height() + 7;
    rbbi.cxIdeal = 200;
    rbbi.cx = 200;
    rebarControl->SetBandInfo (0, &rbbi);

    theApp.plugins.SendMessage("*", "* OnCreate", "DoRebar", (long)m_wndReBar.GetReBarCtrl().m_hWnd);

    m_wndReBar.RestoreBandSizes();

    m_wndReBar.LockBars(theApp.preferences.GetBool(PREF_TOOLBAND_LOCKED, false));

    LoadBackImage();
    SetBackImage();

    // Create the status bar with two panes - one pane for actual status
    // text msgs. and the other for the progress control
    if (!m_wndStatusBar.CreateEx(this) ||
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

    // The third pane(i.e. at index 2) of the status bar will have
    // the security lock icon displayed in it. Set up it's size(16)
    // and style(no border)so that the padlock icons can be properly drawn
    m_wndStatusBar.SetPaneInfo(2, -1, SBPS_NORMAL|SBPS_NOBORDERS, 16);

    // Create the tooltip window
    m_wndToolTip.Create(&m_wndBrowserView);

    // Also, set the padlock icon to be the insecure icon to begin with
    UpdateSecurityStatus(nsIWebProgressListener::STATE_IS_INSECURE);

    // Based on the "chromeMask" we were supplied during construction
    // hide any requested UI elements - statusbar, menubar etc...
    // Note that the window styles (WM_RESIZE etc) are set inside
    // of PreCreateWindow()

    SetupFrameChrome();

    return 0;
}


void CBrowserFrame::SetupFrameChrome()
{
    if(m_chromeMask == nsIWebBrowserChrome::CHROME_ALL)
        return;
    
    if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_TOOLBAR) &&  
       ! (m_chromeMask & nsIWebBrowserChrome::CHROME_MENUBAR) && 
       ! (m_chromeMask & nsIWebBrowserChrome::CHROME_LOCATIONBAR) ) {
        m_wndReBar.ShowWindow(SW_HIDE); // Hide the Rebar
    }
    
    if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_TOOLBAR) ) {
        int i = m_wndReBar.count();
        int iMenubar = m_wndReBar.FindByName("Menu");
        int iUrlbar  = m_wndReBar.FindByName("URL Bar");
        while (i-- > 0) {
            if (i != iMenubar && i != iUrlbar)
                m_wndReBar.ShowBand(i, false);
        }
        m_wndReBar.lineup();
        m_wndReBar.LockBars(true);
    }
    
    if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_MENUBAR) ) {
        SetMenu(NULL); // Hide the MenuBar
        int iMenubar  = m_wndReBar.FindByName("Menu");
        if (iMenubar >= 0)
            m_wndReBar.ShowBand(iMenubar, false);
    }
    
    if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_LOCATIONBAR) ) {
        int iUrlbar  = m_wndReBar.FindByName("URL Bar");
        if (iUrlbar >= 0)
            m_wndReBar.ShowBand(iUrlbar, false);
    }
    
    if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_STATUSBAR) )
        m_wndStatusBar.ShowWindow(SW_HIDE); // Hide the StatusBar
}

BOOL CBrowserFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if( !CFrameWnd::PreCreateWindow(cs) )
        return FALSE;
 
    cs.lpszClass = BROWSER_WINDOW_CLASS;
    cs.dwExStyle &= ~WS_EX_CLIENTEDGE;

    // Change window style based on the chromeMask

    if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_TITLEBAR) )
        cs.style &= ~WS_CAPTION; // No caption      

    cs.style |= WS_SIZEBOX;
    cs.style |= WS_THICKFRAME;
    cs.style |= WS_MINIMIZEBOX;
    cs.style |= WS_MAXIMIZEBOX;

    if (!theApp.preferences.bDisableResize) {   
        if(! (m_chromeMask & nsIWebBrowserChrome::CHROME_WINDOW_RESIZE) ) {
            // Can't resize this window
            cs.style &= ~WS_SIZEBOX;
            cs.style &= ~WS_THICKFRAME;
            // cs.style &= ~WS_MINIMIZEBOX;
            cs.style &= ~WS_MAXIMIZEBOX;
        }
    }

//  cs.lpszClass = AfxRegisterWndClass(0);

    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CBrowserFrame message handlers
void CBrowserFrame::OnSetFocus(CWnd* pOldWnd)
{
    if (theApp.m_pMostRecentBrowserFrame != this) {
        theApp.m_pMostRecentBrowserFrame = this;

        // update session history for the current window
        PostMessage(UWM_UPDATESESSIONHISTORY, 0, 0);
    }

    // forward focus to the browser window
    m_wndBrowserView.SetFocus();
}

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
void CBrowserFrame::OnSize(UINT nType, int cx, int cy)
{
    CFrameWnd::OnSize(nType, cx, cy);
 
    // Get the ItemRect of the status bar's Pane 0
    // That's where the progress bar will be located
    RECT rc;
    if (m_wndStatusBar.m_hWnd)
        m_wndStatusBar.GetItemRect(m_wndStatusBar.CommandToIndex(ID_PROG_BAR), &rc);

    // Move the progress bar into it's correct location
    //
    if (m_wndProgressBar.m_hWnd)
        m_wndProgressBar.MoveWindow(&rc);

    if (!m_created) return;

    // only record the window size for non-popup windows
    if (!(m_style & WS_POPUP)){
        // record the maximized state
        if (nType == SIZE_MAXIMIZED)
            theApp.preferences.bMaximized = true;
        // record the window size/pos
        else if (nType == SIZE_RESTORED) {
            theApp.preferences.bMaximized = false;

            GetWindowRect(&rc);
            if (rc.left > 0 && rc.top > 0 ) {
                theApp.preferences.windowWidth = rc.right - rc.left;
                theApp.preferences.windowHeight = rc.bottom - rc.top;
                theApp.preferences.windowXPos = rc.left;
                theApp.preferences.windowYPos = rc.top;
            }
        }
    }
}

#if 0
void CBrowserFrame::OnMove(int x, int y)
{ 
    CFrameWnd::OnMove(x, y);

    WINDOWPLACEMENT wp;
    wp.length = sizeof (WINDOWPLACEMENT);
    GetWindowPlacement(&wp);

    // only record the window position for non-popup windows
    if (!(m_style & WS_POPUP) && (wp.showCmd != SW_SHOWMAXIMIZED)){
        RECT rc;

        GetWindowRect(&rc);
        if (rc.left > 0 && rc.top > 0 ) {
            theApp.preferences.windowWidth = rc.right - rc.left;
            theApp.preferences.windowHeight = rc.bottom - rc.top;
            theApp.preferences.windowXPos = rc.left;
            theApp.preferences.windowYPos = rc.top;
        }
    }
}
#endif

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

void CBrowserFrame::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized) 
{
	if (nState != WA_INACTIVE && theApp.m_pMostRecentBrowserFrame != this) {
        theApp.m_pMostRecentBrowserFrame = this;

        // update session history for the current window
        PostMessage(UWM_UPDATESESSIONHISTORY, 0, 0);
    }

   CFrameWnd::OnActivate(nState, pWndOther, bMinimized);
   if (pWndOther == this)
	   return;

   	if(bMinimized) // This isn't an activate event that Gecko cares about
	{
		// When we get there, the focus is already lost !!
		// So CBrowserFrame::OnSysCommand take care of it
		return;
	}

    switch(nState) {
        case WA_ACTIVE:
        case WA_CLICKACTIVE:
			if (!m_wndLastFocused || m_wndLastFocused == m_wndBrowserView.m_hWnd 
				|| ::IsChild(m_wndBrowserView.m_hWnd, m_wndLastFocused)) {
				m_wndBrowserView.Activate(TRUE);
			}
			else
				::SetFocus(m_wndLastFocused);
			break;
		case WA_INACTIVE:
			m_wndLastFocused = ::GetFocus();
			if ( ::IsChild(m_wndBrowserView.m_hWnd, m_wndLastFocused) 
				|| m_wndLastFocused == m_wndBrowserView.m_hWnd)
				m_wndBrowserView.Activate(FALSE);

			break;
		default:
            break;
	}
  // m_wndBrowserView.Activate(nState, pWndOther, bMinimized);
}

#define IS_SECURE(state) ((state & 0xFFFF) == nsIWebProgressListener::STATE_IS_SECURE)
void CBrowserFrame::UpdateSecurityStatus(PRInt32 aState)
{
   int iResID = nsIWebProgressListener::STATE_IS_INSECURE;

   if(IS_SECURE(aState)) {
      iResID = IDR_SECURITY_LOCK;
      m_wndBrowserView.m_SecurityState = CBrowserView::SECURITY_STATE_SECURE;
	  m_wndUrlBar.Highlight(1);
   }
   else if(aState == nsIWebProgressListener::STATE_IS_INSECURE) {
      iResID = IDR_SECURITY_UNLOCK;
      m_wndBrowserView.m_SecurityState = CBrowserView::SECURITY_STATE_INSECURE;
	  m_wndUrlBar.Highlight(0);
   }
   else if(aState == nsIWebProgressListener::STATE_IS_BROKEN) {
      iResID = IDR_SECURITY_BROKEN;
      m_wndBrowserView.m_SecurityState = CBrowserView::SECURITY_STATE_BROKEN;
	  m_wndUrlBar.Highlight(2);
   }

   CStatusBarCtrl& sb = m_wndStatusBar.GetStatusBarCtrl();
   HICON hTmpSecurityIcon = 
    (HICON)::LoadImage(AfxGetResourceHandle(),
       MAKEINTRESOURCE(iResID), IMAGE_ICON, 16,16,LR_LOADMAP3DCOLORS);
   sb.SetIcon(2, //2 is the pane index of the status bar where the lock icon will be shown
       hTmpSecurityIcon);
   if (m_hSecurityIcon)
       DestroyIcon(m_hSecurityIcon);
   m_hSecurityIcon = hTmpSecurityIcon;

   m_wndStatusBar.RedrawWindow();
}

void CBrowserFrame::ShowSecurityInfo()
{
    m_wndBrowserView.ShowSecurityInfo();
}

// CMyStatusBar Class
CMyStatusBar::CMyStatusBar() 
{
}

CMyStatusBar::~CMyStatusBar() 
{
}

BEGIN_MESSAGE_MAP(CMyStatusBar, CStatusBar)
   //{{AFX_MSG_MAP(CMyStatusBar)
   ON_WM_LBUTTONDOWN()
   //}}AFX_MSG_MAP
END_MESSAGE_MAP()

void CMyStatusBar::OnLButtonDown(UINT nFlags, CPoint point)
{
    // Check to see if the mouse click was within the
    // padlock icon pane(at pane index 2) of the status bar...

    RECT rc;
    GetItemRect(2, &rc );

    if(PtInRect(&rc, point)) 
    {
        CBrowserFrame *pFrame = (CBrowserFrame *)GetParent();
        if(pFrame != NULL)
            pFrame->ShowSecurityInfo();
    }

    CStatusBar::OnLButtonDown(nFlags, point);
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
	if (m_bmpBack.GetSafeHandle() == NULL)
		return;

    CReBarCtrl& rc = m_wndReBar.GetReBarCtrl ();

    REBARBANDINFO info;
    memset (&info, 0, sizeof (REBARBANDINFO));
    info.cbSize = sizeof (info);
    info.hbmBack = theApp.preferences.bToolbarBackground ? (HBITMAP)m_bmpBack : NULL;

    int count = rc.GetBandCount();
    for (int i = 0; i < count; i++)  {
        info.fMask = RBBIM_BACKGROUND;
        BOOL blah = rc.SetBandInfo (i, &info);

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
        m_bmpBack.DeleteObject ();

	CString skinFile;
	if (theApp.FindSkinFile(skinFile, _T("Back.bmp")))
	{
		HBITMAP hbmp = (HBITMAP) ::LoadImage (NULL,
						skinFile, IMAGE_BITMAP, 0, 0,
						LR_LOADMAP3DCOLORS | LR_LOADFROMFILE);
		m_bmpBack.Attach (hbmp);
	}
}

LRESULT CBrowserFrame::RefreshToolBarItem(WPARAM ItemID, LPARAM unused)
{
    // Drop this through to BrowserView
    m_wndBrowserView.RefreshToolBarItem(ItemID, unused);

    return 0;
}

LRESULT CBrowserFrame::RefreshMRUList(WPARAM ItemID, LPARAM unused)
{
    theApp.m_MRUList->RefreshURLs();
    m_wndUrlBar.RefreshMRUList();

    return 0;
}

void CBrowserFrame::SetSoftFocus() {
   if (IsIconic() || !IsWindowVisible())
      return;
   HWND toplevelWnd = m_hWnd;
   while (::GetParent(toplevelWnd))
      toplevelWnd = ::GetParent(toplevelWnd);
   if (toplevelWnd != ::GetForegroundWindow() || 
      toplevelWnd != ::GetActiveWindow())
      return;
   SetFocus();
}

void CBrowserFrame::ToggleToolBar(UINT uID)
{
    m_wndReBar.ToggleVisibility(uID - TOOLBAR_MENU_START_ID);
}

void CBrowserFrame::ToggleToolbarLock()
{
    BOOL locked = theApp.preferences.GetBool(PREF_TOOLBAND_LOCKED, false);
    if (!locked)
        m_wndReBar.SaveBandSizes();
    locked = !locked;
    theApp.preferences.SetBool(PREF_TOOLBAND_LOCKED, locked);
    m_wndReBar.LockBars(locked);
}

void CBrowserFrame::OnUpdateToolBarMenu(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(
		m_wndReBar.GetVisibility(pCmdUI->m_nID - TOOLBAR_MENU_START_ID)
	);
}
void CBrowserFrame::OnUpdateToggleToolbarLock(CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(theApp.preferences.GetBool(PREF_TOOLBAND_LOCKED, false));
}


BOOL CALLBACK EnumToolbar(HWND hwnd, LPARAM lParam)
{
	TCHAR pszName[20];
	GetClassName(hwnd, pszName, 20);
	if ( GetParent(hwnd) == GetParent((HWND)lParam) && _tcscmp(TOOLBARCLASSNAME, pszName) == 0
		&& ( (GetWindowLong(hwnd, GWL_STYLE) & CCS_BOTTOM) == CCS_BOTTOM))
	{
		SetWindowPos((HWND)lParam, hwnd ,0,0,0,0,SWP_NOMOVE);
		//return FALSE;
	}
	return TRUE;
}

// create a linked list of CToolBarEx controls, return the hwnd
HWND CBrowserFrame::CreateToolbar(UINT style) {
    return m_tbList.Add(this, style);
}

#include "nsITypeAheadFind.h"

void CBrowserFrame::OnShowFindBar()
{
   // When the the user chooses the Find menu item
    // and if a Find dlg. is already being shown
    // just set focus to the existing dlg instead of
    // creating a new one
    if(m_wndFindBar)
    {
		m_wndFindBar->SetFocus();
		return;
    }

    CString csSearchStr;
    PRBool bMatchCase = PR_FALSE;
    PRBool bMatchWholeWord = PR_FALSE;
    PRBool bWrapAround = PR_TRUE;
    //PRBool bSearchBackwards = PR_FALSE;
#ifdef FINDBAR_USE_TYPEAHEAD
	nsresult rv;
    nsCOMPtr<nsITypeAheadFind> tafinder = do_GetService(NS_TYPEAHEADFIND_CONTRACTID, &rv);
	if (NS_SUCCEEDED(rv))
	{
		PRBool b;
		tafinder->GetIsActive(&b);
		if (b == PR_FALSE)
		{
			nsCOMPtr<nsIDOMWindow> dom(do_GetInterface(m_wndBrowserView.mWebBrowser));
			tafinder->StartNewFind(dom, PR_FALSE);
		}
	}
#endif
	// See if we can get and initialize the dlg box with
    // the values/settings the user specified in the previous search
	nsCOMPtr<nsIWebBrowserFind> finder(do_GetInterface(m_wndBrowserView.mWebBrowser));	
    if(finder)
    {
		PRUnichar *stringBuf = nsnull;
        finder->GetSearchString(&stringBuf);
        csSearchStr = stringBuf;
        nsMemory::Free(stringBuf);

		finder->GetMatchCase(&bMatchCase);
		finder->GetEntireWord(&bMatchWholeWord);
		finder->GetWrapFind(&bWrapAround);
		//finder->GetFindBackwards(&bSearchBackwards);		
    }

	// Create the find bar
	m_wndFindBar = new CFindRebar(csSearchStr, bMatchCase, bMatchWholeWord, bWrapAround, this);
	m_wndFindBar->Create(this, CBRS_BOTTOM);

	// It must stay above the sidebar 
	m_wndFindBar->SetWindowPos(&m_wndStatusBar ,0,0,0,0,SWP_NOMOVE);
	// And above any bottom toolbar ?
	EnumChildWindows(m_hWnd, EnumToolbar, (LPARAM)m_wndFindBar->m_hWnd); 

	// Update the layout
	RecalcLayout();
}

void CBrowserFrame::ClearFindBar()
{
	m_wndFindBar = NULL;
	RecalcLayout();
}

void CBrowserFrame::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (nID == SC_MINIMIZE) {
		// We're taking care of the focus here, because it will
		// be lost after that and not correctly memorized.
        m_wndLastFocused = ::GetFocus();
		if (::IsChild(m_wndBrowserView.m_hWnd, m_wndLastFocused))
		   m_wndBrowserView.Activate(FALSE);
	}

	CFrameWnd::OnSysCommand(nID, lParam);
}
