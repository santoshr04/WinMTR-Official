//*****************************************************************************
// FILE:            WinMTRDialog.cpp
//
//
//*****************************************************************************

#include "WinMTRGlobal.h"
#include "WinMTRDialog.h"
#include "WinMTROptions.h"
#include "WinMTRProperties.h"
#include "WinMTRNet.h"
#include <iostream>
#include <sstream>
#include <vector>
#include "afxlinkctrl.h"

#define TRACE_MSG(msg)										\
	{														\
	std::ostringstream dbg_msg(std::ostringstream::out);	\
	dbg_msg << msg << std::endl;							\
	OutputDebugString(dbg_msg.str().c_str());				\
	}

#ifdef _DEBUG
#define new DEBUG_NE
#undef THIS_FILE
static	 char THIS_FILE[] = __FILE__;
#endif

void PingThread(void *p);

struct PingThreadData {
	WinMTRDialog* dialog;
	int sessionIdx;
	char hostname[256];
};

//*****************************************************************************
// BEGIN_MESSAGE_MAP
//
// 
//*****************************************************************************
BEGIN_MESSAGE_MAP(WinMTRDialog, CDialog)
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_SIZING()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(ID_RESTART, OnRestart)
	ON_BN_CLICKED(ID_OPTIONS, OnOptions)
	ON_BN_CLICKED(ID_CTTC, OnCTTC)
	ON_BN_CLICKED(ID_CHTC, OnCHTC)
	ON_BN_CLICKED(ID_EXPT, OnEXPT)
	ON_BN_CLICKED(ID_EXPH, OnEXPH)
	ON_NOTIFY(NM_DBLCLK, IDC_LIST_MTR, OnDblclkList)
	ON_CBN_SELCHANGE(IDC_COMBO_HOST, &WinMTRDialog::OnCbnSelchangeComboHost)
	ON_CBN_SELENDOK(IDC_COMBO_HOST, &WinMTRDialog::OnCbnSelendokComboHost)
	ON_CBN_CLOSEUP(IDC_COMBO_HOST, &WinMTRDialog::OnCbnCloseupComboHost)
	ON_WM_TIMER()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDCANCEL, &WinMTRDialog::OnBnClickedCancel)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_LIST_MTR, OnCustomDrawList)
END_MESSAGE_MAP()


//*****************************************************************************
// WinMTRDialog::WinMTRDialog
//
// 
//*****************************************************************************
WinMTRDialog::WinMTRDialog(CWnd* pParent) 
			: CDialog(WinMTRDialog::IDD, pParent),
			state(IDLE),
			transition(IDLE_TO_IDLE),
			activeSession(-1)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_autostart = 0;
	useDNS = DEFAULT_DNS;
	interval = DEFAULT_INTERVAL;
	pingsize = DEFAULT_PING_SIZE;
	maxLRU = DEFAULT_MAX_LRU;
	nrLRU = 0;

	hasIntervalFromCmdLine = false;
	hasPingsizeFromCmdLine = false;
	hasMaxLRUFromCmdLine = false;
	hasUseDNSFromCmdLine = false;

	traceThreadMutex = CreateMutex(NULL, FALSE, NULL);
}

WinMTRDialog::~WinMTRDialog()
{
	for (size_t i = 0; i < sessions.size(); i++) {
		if (sessions[i]->wmtrnet) {
			sessions[i]->wmtrnet->StopTrace();
			delete sessions[i]->wmtrnet;
		}
		if (sessions[i]->traceMutex) CloseHandle(sessions[i]->traceMutex);
		delete sessions[i];
	}
	sessions.clear();
	CloseHandle(traceThreadMutex);
}

//*****************************************************************************
// WinMTRDialog::DoDataExchange
//
// 
//*****************************************************************************
void WinMTRDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, ID_OPTIONS, m_buttonOptions);
	DDX_Control(pDX, IDCANCEL, m_buttonExit);
	DDX_Control(pDX, ID_RESTART, m_buttonStart);
	DDX_Control(pDX, IDC_COMBO_HOST, m_comboHost);
	DDX_Control(pDX, IDC_LIST_MTR, m_listMTR);
	DDX_Control(pDX, IDC_STATICS, m_staticS);
	DDX_Control(pDX, IDC_STATICJ, m_staticJ);
	DDX_Control(pDX, ID_EXPH, m_buttonExpH);
	DDX_Control(pDX, ID_EXPT, m_buttonExpT);
}

//*****************************************************************************
// WinMTRDialog::RefreshTabs
//
//*****************************************************************************
void WinMTRDialog::RefreshTabs()
{
	int curSel = m_tabCtrl.GetCurSel();
	m_tabCtrl.DeleteAllItems();
	for (size_t i = 0; i < sessions.size(); i++) {
		CString label = sessions[i]->hostname;
		if (label.GetLength() > 25) label = label.Left(22) + "...";
		m_tabCtrl.InsertItem((int)i, label);
	}
	m_tabCtrl.InsertItem((int)sessions.size(), _T("+"));
	if (curSel >= 0 && curSel < m_tabCtrl.GetItemCount()) {
		m_tabCtrl.SetCurSel(curSel);
	}
}

//*****************************************************************************
// WinMTRDialog::StartTraceForSession
//
//*****************************************************************************
void WinMTRDialog::StartTraceForSession(int idx, const char* hostname)
{
	if (idx < 0 || idx >= (int)sessions.size()) return;

	TraceSession* sess = sessions[idx];
	sess->hostname = hostname;
	sess->tracing = true;

	if (!sess->wmtrnet) {
		sess->wmtrnet = new WinMTRNet(this);
	}

	PingThreadData* ptd = new PingThreadData;
	ptd->dialog = this;
	ptd->sessionIdx = idx;
	strncpy(ptd->hostname, hostname, 255);
	ptd->hostname[255] = '\0';

	_beginthread(PingThread, 0, ptd);

	RefreshTabs();
	m_tabCtrl.SetCurSel(idx);
	activeSession = idx;
	m_listMTR.DeleteAllItems();
}

//*****************************************************************************
// WinMTRDialog::CloseSession
//
//*****************************************************************************
void WinMTRDialog::CloseSession(int idx)
{
	if (idx < 0 || idx >= (int)sessions.size()) return;
	if (sessions.size() <= 1) return; // keep at least one session

	TraceSession* sess = sessions[idx];
	if (sess->wmtrnet) {
		sess->wmtrnet->StopTrace();
		Sleep(100);
		delete sess->wmtrnet;
	}
	if (sess->traceMutex) CloseHandle(sess->traceMutex);
	delete sess;

	sessions.erase(sessions.begin() + idx);

	if (activeSession >= (int)sessions.size()) activeSession = (int)sessions.size() - 1;
	if (activeSession < 0) activeSession = 0;

	RefreshTabs();
	m_tabCtrl.SetCurSel(activeSession);
	m_listMTR.DeleteAllItems();
	if (activeSession >= 0 && sessions[activeSession]->wmtrnet) {
		DisplayRedraw();
	}
}


//*****************************************************************************
// WinMTRDialog::OnInitDialog
//
// 
//*****************************************************************************
BOOL WinMTRDialog::OnInitDialog()
{
	CDialog::OnInitDialog();

	SetTimer(1, WINMTR_DIALOG_TIMER, NULL);
	SetWindowText(_T("WinMTR - Network Diagnostic Tool"));

	SetIcon(m_hIcon, TRUE);			
	SetIcon(m_hIcon, FALSE);
	
	if(!statusBar.Create( this ))
		AfxMessageBox("Error creating status bar");
	statusBar.GetStatusBarCtrl().SetMinHeight(22);
		
	UINT sbi[1];
	sbi[0] = IDS_STRING_SB_NAME;	
	statusBar.SetIndicators( sbi,1);
	statusBar.SetPaneInfo(0, statusBar.GetItemID(0),SBPS_STRETCH, NULL );

	// Create tab control
	CRect rcTab;
	m_listMTR.GetWindowRect(&rcTab);
	ScreenToClient(&rcTab);
	CRect rcTabCtrl(rcTab.left, rcTab.top - 24, rcTab.Width() - 21 + rcTab.left, rcTab.top - 2);
	m_tabCtrl.Create(WS_CHILD|WS_VISIBLE|TCS_TABS|TCS_SINGLELINE, rcTabCtrl, this, 2000);
	
	CFont* tabFont = new CFont();
	tabFont->CreatePointFont(85, _T("Segoe UI"));
	m_tabCtrl.SetFont(tabFont);

	// Create initial session
	TraceSession* defSession = new TraceSession;
	defSession->hostname = "New Host";
	defSession->wmtrnet = NULL;
	defSession->tracing = false;
	defSession->traceMutex = CreateMutex(NULL, FALSE, NULL);
	defSession->tabIndex = 0;
	sessions.push_back(defSession);
	activeSession = 0;

	RefreshTabs();
	m_tabCtrl.SetCurSel(0);

	// Summary text control
	CRect rcSummary(rcTab.left + 10, rcTab.top + 10, rcTab.right - 10, rcTab.bottom - 10);
	m_summaryText.Create(_T(""), WS_CHILD|WS_VISIBLE|SS_LEFT, rcSummary, this, 2001);
	m_summaryText.SetFont(tabFont);
	m_summaryText.ShowWindow(SW_HIDE);

	// Style the list control
	m_listMTR.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
	CFont* listFont = new CFont();
	listFont->CreatePointFont(85, _T("Segoe UI"));
	m_listMTR.SetFont(listFont);

	for(int i = 0; i< MTR_NR_COLS; i++)
		m_listMTR.InsertColumn(i, MTR_COLS[i], LVCFMT_LEFT, MTR_COL_LENGTH[i] , -1);
   
	m_comboHost.SetFocus();

	// Adjust window for control bars
	CRect rcClientStart, rcClientNow;
	GetClientRect(rcClientStart);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST,
				   0, reposQuery, rcClientNow);

	CPoint ptOffset(rcClientNow.left - rcClientStart.left,
					rcClientNow.top - rcClientStart.top);

	CRect rcChild;
	CWnd* pwndChild = GetWindow(GW_CHILD);
	while (pwndChild) {
		pwndChild->GetWindowRect(rcChild);
		ScreenToClient(rcChild);
		rcChild.OffsetRect(ptOffset);
		pwndChild->MoveWindow(rcChild, FALSE);
		pwndChild = pwndChild->GetNextWindow();
	}

	CRect rcWindow;
	GetWindowRect(rcWindow);
	rcWindow.right += rcClientStart.Width() - rcClientNow.Width();
	rcWindow.bottom += rcClientStart.Height() - rcClientNow.Height();
	MoveWindow(rcWindow, FALSE);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0);

	InitRegistry();

	if (m_autostart) {
		m_comboHost.SetWindowText(msz_defaulthostname);
		OnRestart();
	}

	return FALSE;
}

// Color thresholds for latency
#define LATENCY_GREEN  50
#define LATENCY_YELLOW 150

COLORREF GetLatencyColor(int ms) {
	if (ms <= 0) return RGB(255, 255, 255);
	if (ms <= LATENCY_GREEN) return RGB(220, 255, 220);
	if (ms <= LATENCY_YELLOW) return RGB(255, 255, 200);
	return RGB(255, 200, 200);
}

// Custom draw handler for color-coded cells and latency bars
void WinMTRDialog::OnCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVCUSTOMDRAW* pLVCD = (NMLVCUSTOMDRAW*)pNMHDR;
	*pResult = CDRF_DODEFAULT;

	if (pLVCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
		*pResult = CDRF_NOTIFYITEMDRAW;
	}
	else if (pLVCD->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
		*pResult = CDRF_NOTIFYSUBITEMDRAW;
	}
	else if (pLVCD->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
		int nItem = (int)pLVCD->nmcd.dwItemSpec;
		int nSubItem = pLVCD->iSubItem;

		if (activeSession < 0 || activeSession >= (int)sessions.size()) return;
		TraceSession* sess = sessions[activeSession];
		if (!sess->wmtrnet) return;

		// Color-code Best(5), Avrg(6), Worst(7), Last(8) columns
		if (nSubItem >= 5 && nSubItem <= 8) {
			if (sess->tracing) {
				int latency = 0;
				switch (nSubItem) {
					case 5: latency = sess->wmtrnet->GetBest(nItem); break;
					case 6: latency = sess->wmtrnet->GetAvg(nItem); break;
					case 7: latency = sess->wmtrnet->GetWorst(nItem); break;
					case 8: latency = sess->wmtrnet->GetLast(nItem); break;
				}
				pLVCD->clrTextBk = GetLatencyColor(latency);
				*pResult = CDRF_NEWFONT;
			}
		}

		// Custom draw latency bar column (9)
		if (nSubItem == 9) {
			CDC* pDC = CDC::FromHandle(pLVCD->nmcd.hdc);
			CRect rc;
			m_listMTR.GetSubItemRect(nItem, nSubItem, LVIR_BOUNDS, rc);
			
			int avg = sess->wmtrnet->GetAvg(nItem);
			int maxLatency = 1;
			int nh = sess->wmtrnet->GetMax();
			for (int j = 0; j < nh; j++) {
				int a = sess->wmtrnet->GetAvg(j);
				if (a > maxLatency) maxLatency = a;
			}
			if (maxLatency < 1) maxLatency = 1;

			// Format text first - needed for width calculation
			char buf[32];
			sprintf(buf, "%d ms", avg);
			CSize textSize = pDC->GetTextExtent(buf, (int)strlen(buf));

			pDC->FillSolidRect(rc, RGB(248, 248, 248));

			int margin = 3;
			int textWidth = textSize.cx + 4;
			int barHeight = max(rc.Height() - 8, 6);
			int barTop = rc.top + (rc.Height() - barHeight) / 2;
			int barLeft = rc.left + margin;
			int barMaxWidth = rc.Width() - margin * 2 - textWidth - 4;
			if (barMaxWidth < 10) barMaxWidth = 10;
			
			int barWidth = (avg * barMaxWidth) / maxLatency;
			if (barWidth < 2 && avg > 0) barWidth = 2;
			if (barWidth > barMaxWidth) barWidth = barMaxWidth;

			// Draw bar
			CRect barRect(barLeft, barTop, barLeft + barWidth, barTop + barHeight);
			COLORREF barColor = GetLatencyColor(avg);
			int r = GetRValue(barColor);
			int g = GetGValue(barColor) - 60;
			int b = GetBValue(barColor) - 60;
			if (g < 0) g = 0;
			if (b < 0) b = 0;
			pDC->FillSolidRect(barRect, RGB(r, g, b));
			pDC->Draw3dRect(barRect, RGB(170,170,170), RGB(170,170,170));

			// Draw text AFTER bar, right-aligned to fit remaining space
			pDC->SetBkMode(TRANSPARENT);
			CFont* pOldFont = pDC->SelectObject(m_listMTR.GetFont());
			CRect textRect(barLeft + barWidth + 3, rc.top, rc.right - 1, rc.bottom);
			pDC->DrawText(buf, -1, textRect, DT_VCENTER | DT_LEFT | DT_SINGLELINE);
			pDC->SelectObject(pOldFont);

			*pResult = CDRF_SKIPDEFAULT;
		}
	}
}

//*****************************************************************************
// WinMTRDialog::InitRegistry (unchanged)
//*****************************************************************************
BOOL WinMTRDialog::InitRegistry()
{
	HKEY hKey, hKey_v;
	DWORD res, tmp_dword, value_size;
	LONG r;

	r = RegCreateKeyEx(HKEY_CURRENT_USER, "Software", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &res);
	if(r != ERROR_SUCCESS) return FALSE;
	r = RegCreateKeyEx(hKey, "WinMTR", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &res);
	if(r != ERROR_SUCCESS) return FALSE;

	RegSetValueEx(hKey,"Version", 0, REG_SZ, (const unsigned char *)WINMTR_VERSION, sizeof(WINMTR_VERSION)+1);
	RegSetValueEx(hKey,"License", 0, REG_SZ, (const unsigned char *)WINMTR_LICENSE, sizeof(WINMTR_LICENSE)+1);
	RegSetValueEx(hKey,"HomePage", 0, REG_SZ, (const unsigned char *)WINMTR_HOMEPAGE, sizeof(WINMTR_HOMEPAGE)+1);

	r = RegCreateKeyEx(hKey, "Config", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey_v, &res);
	if(r != ERROR_SUCCESS) return FALSE;

	if(RegQueryValueEx(hKey_v, "PingSize", 0, NULL, (unsigned char *)&tmp_dword, &value_size) != ERROR_SUCCESS) {
		tmp_dword = pingsize;
		RegSetValueEx(hKey_v,"PingSize", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
	} else { if(!hasPingsizeFromCmdLine) pingsize = tmp_dword; }
	
	if(RegQueryValueEx(hKey_v, "MaxLRU", 0, NULL, (unsigned char *)&tmp_dword, &value_size) != ERROR_SUCCESS) {
		tmp_dword = maxLRU;
		RegSetValueEx(hKey_v,"MaxLRU", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
	} else { if(!hasMaxLRUFromCmdLine) maxLRU = tmp_dword; }
	
	if(RegQueryValueEx(hKey_v, "UseDNS", 0, NULL, (unsigned char *)&tmp_dword, &value_size) != ERROR_SUCCESS) {
		tmp_dword = useDNS ? 1 : 0;
		RegSetValueEx(hKey_v,"UseDNS", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
	} else { if(!hasUseDNSFromCmdLine) useDNS = (BOOL)tmp_dword; }

	if(RegQueryValueEx(hKey_v, "Interval", 0, NULL, (unsigned char *)&tmp_dword, &value_size) != ERROR_SUCCESS) {
		tmp_dword = (DWORD)(interval * 1000);
		RegSetValueEx(hKey_v,"Interval", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
	} else { if(!hasIntervalFromCmdLine) interval = (float)tmp_dword / 1000.0; }

	r = RegCreateKeyEx(hKey, "LRU", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey_v, &res);
	if(r != ERROR_SUCCESS) return FALSE;
	if(RegQueryValueEx(hKey_v, "NrLRU", 0, NULL, (unsigned char *)&tmp_dword, &value_size) != ERROR_SUCCESS) {
		tmp_dword = nrLRU;
		RegSetValueEx(hKey_v,"NrLRU", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
	} else {
		char key_name[20];
		unsigned char str_host[255];
		nrLRU = tmp_dword;
		for(int i=0;i<maxLRU;i++) {
			sprintf(key_name,"Host%d", i+1);
			if((r = RegQueryValueEx(hKey_v, key_name, 0, NULL, NULL, &value_size)) == ERROR_SUCCESS) {
				RegQueryValueEx(hKey_v, key_name, 0, NULL, str_host, &value_size);
				str_host[value_size]='\0';
				m_comboHost.AddString((CString)str_host);
			}
		}
	}
	m_comboHost.AddString(CString((LPCSTR)IDS_STRING_CLEAR_HISTORY));
	RegCloseKey(hKey_v);
	RegCloseKey(hKey);
	return TRUE;
}

//*****************************************************************************
// OnSizing, OnSize, OnPaint, OnQueryDragIcon (unchanged)
//*****************************************************************************
void WinMTRDialog::OnSizing(UINT fwSide, LPRECT pRect) 
{
	CDialog::OnSizing(fwSide, pRect);
	int iWidth = (pRect->right)-(pRect->left);
	int iHeight = (pRect->bottom)-(pRect->top);
	if (iWidth < 600) pRect->right = pRect->left + 600;
	if (iHeight <250) pRect->bottom = pRect->top + 250;
}

void WinMTRDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	CRect r;
	GetClientRect(&r);
	CRect lb;
	
	if (::IsWindow(m_staticS.m_hWnd)) {
		m_staticS.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_staticS.SetWindowPos(NULL, lb.TopLeft().x, lb.TopLeft().y, r.Width()-lb.TopLeft().x-10, lb.Height() , SWP_NOMOVE | SWP_NOZORDER);
	}
	if (::IsWindow(m_staticJ.m_hWnd)) {
		m_staticJ.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_staticJ.SetWindowPos(NULL, lb.TopLeft().x, lb.TopLeft().y, r.Width() - 21, lb.Height(), SWP_NOMOVE | SWP_NOZORDER);
	}
	if (::IsWindow(m_buttonExit.m_hWnd)) {
		m_buttonExit.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_buttonExit.SetWindowPos(NULL, r.Width() - lb.Width()-21, lb.TopLeft().y, lb.Width(), lb.Height() , SWP_NOSIZE | SWP_NOZORDER);
	}
	if (::IsWindow(m_buttonExpH.m_hWnd)) {
		m_buttonExpH.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_buttonExpH.SetWindowPos(NULL, r.Width() - lb.Width()-21, lb.TopLeft().y, lb.Width(), lb.Height() , SWP_NOSIZE | SWP_NOZORDER);
	}
	if (::IsWindow(m_buttonExpT.m_hWnd)) {
		m_buttonExpT.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_buttonExpT.SetWindowPos(NULL, r.Width() - lb.Width()- 103, lb.TopLeft().y, lb.Width(), lb.Height() , SWP_NOSIZE | SWP_NOZORDER);
	}
	if (::IsWindow(m_tabCtrl.m_hWnd)) {
		m_tabCtrl.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_tabCtrl.SetWindowPos(NULL, lb.TopLeft().x, lb.TopLeft().y, r.Width() - 21 - lb.TopLeft().x, lb.Height(), SWP_NOMOVE | SWP_NOZORDER);
	}
	if (::IsWindow(m_listMTR.m_hWnd)) {
		m_listMTR.GetWindowRect(&lb);
		ScreenToClient(&lb);
		m_listMTR.SetWindowPos(NULL, lb.TopLeft().x, lb.TopLeft().y, r.Width() - 21, r.Height() - lb.top - 25, SWP_NOMOVE | SWP_NOZORDER);
	}
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0, reposQuery, r);
	RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0);
}

void WinMTRDialog::OnPaint() 
{
	if (IsIconic()) {
		CPaintDC dc(this);
		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		dc.DrawIcon((rect.Width() - cxIcon + 1) / 2, (rect.Height() - cyIcon + 1) / 2, m_hIcon);
	} else {
		CDialog::OnPaint();
	}
}

HCURSOR WinMTRDialog::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

//*****************************************************************************
// WinMTRDialog::OnDblclkList
//
//*****************************************************************************
void WinMTRDialog::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (activeSession < 0 || activeSession >= (int)sessions.size()) return;
	TraceSession* sess = sessions[activeSession];
	if (!sess->wmtrnet) return;

	if(sess->tracing) {
		POSITION pos = m_listMTR.GetFirstSelectedItemPosition();
		if(pos!=NULL) {
			int nItem = m_listMTR.GetNextSelectedItem(pos);
			WinMTRProperties wmtrprop;
			if(sess->wmtrnet->GetAddr(nItem)==0) {
				strcpy(wmtrprop.host,"");
				strcpy(wmtrprop.ip,"");
				sess->wmtrnet->GetName(nItem, wmtrprop.comment);
				wmtrprop.pck_loss = wmtrprop.pck_sent = wmtrprop.pck_recv = 0;
				wmtrprop.ping_avrg = wmtrprop.ping_last = 0.0;
				wmtrprop.ping_best = wmtrprop.ping_worst = 0.0;
			} else {
				sess->wmtrnet->GetName(nItem, wmtrprop.host);
				int addr = sess->wmtrnet->GetAddr(nItem);
				sprintf(wmtrprop.ip, "%d.%d.%d.%d", (addr >> 24) & 0xff, (addr >> 16) & 0xff, (addr >> 8) & 0xff, addr & 0xff);
				strcpy(wmtrprop.comment, "Host alive.");
				wmtrprop.ping_avrg = (float)sess->wmtrnet->GetAvg(nItem); 
				wmtrprop.ping_last = (float)sess->wmtrnet->GetLast(nItem); 
				wmtrprop.ping_best = (float)sess->wmtrnet->GetBest(nItem);
				wmtrprop.ping_worst = (float)sess->wmtrnet->GetWorst(nItem); 
				wmtrprop.pck_loss = sess->wmtrnet->GetPercent(nItem);
				wmtrprop.pck_recv = sess->wmtrnet->GetReturned(nItem);
				wmtrprop.pck_sent = sess->wmtrnet->GetXmit(nItem);
			}
			wmtrprop.DoModal();
		}
	}
}

// Setters (unchanged)
void WinMTRDialog::SetHostName(const char *host) { m_autostart = 1; strncpy(msz_defaulthostname,host,1000); }
void WinMTRDialog::SetPingSize(int ps) { pingsize = ps; }
void WinMTRDialog::SetMaxLRU(int mlru) { maxLRU = mlru; }
void WinMTRDialog::SetInterval(float i) { interval = i; }
void WinMTRDialog::SetUseDNS(BOOL udns) { useDNS = udns; }

//*****************************************************************************
// WinMTRDialog::OnRestart - Start/Stop trace on active tab, or add new
//*****************************************************************************
void WinMTRDialog::OnRestart() 
{
	// Handle add-new-tab
	bool isPlusTab = (m_tabCtrl.GetCurSel() == m_tabCtrl.GetItemCount() - 1);
	
	if (isPlusTab) {
		if (sessions.size() >= MAX_SESSIONS) {
			AfxMessageBox("Maximum of 10 concurrent traces reached.");
			return;
		}
		TraceSession* ns = new TraceSession;
		ns->hostname = "New Host";
		ns->wmtrnet = NULL;
		ns->tracing = false;
		ns->traceMutex = CreateMutex(NULL, FALSE, NULL);
		ns->tabIndex = (int)sessions.size();
		sessions.push_back(ns);
		activeSession = (int)sessions.size() - 1;
		RefreshTabs();
		m_tabCtrl.SetCurSel(activeSession);
		m_listMTR.DeleteAllItems();
		EnableIdleControls();
		return;
	}

	if(m_comboHost.GetCurSel() == m_comboHost.GetCount() - 1) {
		ClearHistory();
		return;
	}

	if (activeSession < 0 || activeSession >= (int)sessions.size()) return;

	CString sHost;
	m_comboHost.GetWindowText(sHost);
	sHost.TrimLeft();
	if(sHost.IsEmpty()) {
		AfxMessageBox("No host specified!");
		m_comboHost.SetFocus();
		return;
	}

	if (!sessions[activeSession]->tracing) {
		// Start new trace on this session
		m_listMTR.DeleteAllItems();
		m_summaryText.ShowWindow(SW_HIDE);
		m_listMTR.ShowWindow(SW_SHOW);

		sessions[activeSession]->hostname = sHost;
		RefreshTabs();

		if(m_comboHost.FindString(-1, sHost) == CB_ERR) {
			m_comboHost.InsertString(m_comboHost.GetCount() - 1, sHost);
			HKEY hKey; DWORD tmp_dword; LONG r; char key_name[20];
			r = RegOpenKeyEx(HKEY_CURRENT_USER, "Software", 0, KEY_ALL_ACCESS,&hKey);
			r = RegOpenKeyEx(hKey, "WinMTR", 0, KEY_ALL_ACCESS, &hKey);
			r = RegOpenKeyEx(hKey, "LRU", 0, KEY_ALL_ACCESS, &hKey);
			if(nrLRU >= maxLRU) nrLRU = 0;
			nrLRU++;
			sprintf(key_name, "Host%d", nrLRU);
			r = RegSetValueEx(hKey,key_name, 0, REG_SZ, (const unsigned char *)(LPCTSTR)sHost, strlen((LPCTSTR)sHost)+1);
			tmp_dword = nrLRU;
			r = RegSetValueEx(hKey,"NrLRU", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
			RegCloseKey(hKey);
		}

		if(InitMTRNet()) {
			sessions[activeSession]->tracing = true;
			if (!sessions[activeSession]->wmtrnet)
				sessions[activeSession]->wmtrnet = new WinMTRNet(this);

			// Spawn the ping thread
			PingThreadData *ptd = new PingThreadData;
			ptd->dialog = this;
			ptd->sessionIdx = activeSession;
			strncpy(ptd->hostname, (LPCSTR)sHost, 255);
			ptd->hostname[255] = '\0';
			_beginthread(PingThread, 0, ptd);

			Transit(TRACING);
		}
	} else {
		// Stop trace on this session
		if (sessions[activeSession]->wmtrnet) {
			sessions[activeSession]->wmtrnet->StopTrace();
			sessions[activeSession]->tracing = false;
		}
		Transit(STOPPING);
	}
}

//*****************************************************************************
// WinMTRDialog::OnOptions (unchanged)
//*****************************************************************************
void WinMTRDialog::OnOptions() 
{
	WinMTROptions optDlg;
	optDlg.SetPingSize(pingsize);
	optDlg.SetInterval(interval);
	optDlg.SetMaxLRU(maxLRU);
	optDlg.SetUseDNS(useDNS);

	if(IDOK == optDlg.DoModal()) {
		pingsize = optDlg.GetPingSize();
		interval = optDlg.GetInterval();
		maxLRU = optDlg.GetMaxLRU();
		useDNS = optDlg.GetUseDNS();
		HKEY hKey;
		DWORD tmp_dword;
		LONG r;
		r = RegOpenKeyEx(HKEY_CURRENT_USER, "Software", 0, KEY_ALL_ACCESS,&hKey);
		r = RegOpenKeyEx(hKey, "WinMTR", 0, KEY_ALL_ACCESS, &hKey);
		r = RegOpenKeyEx(hKey, "Config", 0, KEY_ALL_ACCESS, &hKey);
		tmp_dword = pingsize;
		RegSetValueEx(hKey,"PingSize", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
		tmp_dword = maxLRU;
		RegSetValueEx(hKey,"MaxLRU", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
		tmp_dword = useDNS ? 1 : 0;
		RegSetValueEx(hKey,"UseDNS", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
		tmp_dword = (DWORD)(interval * 1000);
		RegSetValueEx(hKey,"Interval", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
		RegCloseKey(hKey);
	}
}

// Clipboard/Export functions (unchanged)
void WinMTRDialog::OnCTTC() 
{	
	if (activeSession < 0 || activeSession >= (int)sessions.size() || !sessions[activeSession]->wmtrnet) return;
	WinMTRNet* wn = sessions[activeSession]->wmtrnet;
	char buf[255], t_buf[1000], f_buf[255*1000];
	char ipinfo[255];
	int nh = wn->GetMax();
	strcpy(f_buf, "|--------------------------------------------------------------------------------------------------------------------|\r\n");
	sprintf(t_buf, "|                                             WinMTR statistics                                                      |\r\n"); strcat(f_buf, t_buf);
	sprintf(t_buf, "| Hostname                        | Nr | Loss%% | Sent | Recv | Best | Avrg | Wrst | Last | Latency | IP Info          |\r\n" ); strcat(f_buf, t_buf);
	sprintf(t_buf, "|---------------------------------|----|-------|------|------|------|------|------|------|---------|------------------|\r\n" ); strcat(f_buf, t_buf);
	for(int i=0;i<nh;i++) {
		wn->GetName(i, buf);
		if(strcmp(buf,"")==0) strcpy(buf,"No response from host");
		wn->GetIpInfo(i, ipinfo);
		if(strlen(ipinfo)==0) strcpy(ipinfo,"...");
		sprintf(t_buf, "| %-31s | %2d | %4d%% | %4d | %4d | %4d | %4d | %4d | %4d | %4dms | %-16s |\r\n",
			buf, i+1, wn->GetPercent(i), wn->GetXmit(i), wn->GetReturned(i),
			wn->GetBest(i), wn->GetAvg(i), wn->GetWorst(i), wn->GetLast(i),
			wn->GetAvg(i), ipinfo);
		strcat(f_buf, t_buf);
	}
	sprintf(t_buf, "|--------------------------------------------------------------------------------------------------------------------|\r\n"); strcat(f_buf, t_buf);
	sprintf(t_buf, "   WinMTR - Network Diagnostic Tool\r\n"); strcat(f_buf, t_buf);
	CString source(f_buf);
	OpenClipboard(); EmptyClipboard();
	HGLOBAL clipbuffer = GlobalAlloc(GMEM_DDESHARE, source.GetLength()+1);
	char* buffer = (char*)GlobalLock(clipbuffer);
	strcpy(buffer, LPCSTR(source));
	GlobalUnlock(clipbuffer);
	SetClipboardData(CF_TEXT, clipbuffer);
	CloseClipboard();
}

void WinMTRDialog::OnCHTC() 
{
	if (activeSession < 0 || activeSession >= (int)sessions.size() || !sessions[activeSession]->wmtrnet) return;
	WinMTRNet* wn = sessions[activeSession]->wmtrnet;
	char buf[255], t_buf[2000], f_buf[255*1000];
	char ipinfo[255];
	int nh = wn->GetMax();
	strcpy(f_buf, "<html><head><title>WinMTR Statistics</title>\r\n");
	strcat(f_buf, "<style>body{font-family:'Segoe UI',Arial,sans-serif;background:#1a1a2e;color:#e0e0e0;margin:20px}");
	strcat(f_buf, "table{border-collapse:collapse;width:100%%}th{background:#16213e;color:#4fd1c5;padding:8px 10px;text-align:left}");
	strcat(f_buf, "td{padding:6px 10px;border-bottom:1px solid #333}");
	strcat(f_buf, "tr:hover{background:#0f3460}.green{color:#4fd1c5}.red{color:#f87171}.yellow{color:#fbbf24}");
	strcat(f_buf, "h2{color:#4fd1c5}</style></head><body>\r\n");
	sprintf(t_buf, "<h2>WinMTR Statistics</h2>\r\n"); strcat(f_buf, t_buf);
	sprintf(t_buf, "<p>Target: %s | Hops: %d</p>\r\n", (LPCSTR)sessions[activeSession]->hostname, nh); strcat(f_buf, t_buf);
	sprintf(t_buf, "<table><tr><th>#</th><th>Hostname</th><th>Loss%%</th><th>Sent</th><th>Recv</th><th>Best</th><th>Avrg</th><th>Worst</th><th>Last</th><th>IP Info</th></tr>\r\n"); strcat(f_buf, t_buf);
	for(int i=0;i<nh;i++) {
		wn->GetName(i, buf);
		if(strcmp(buf,"")==0) strcpy(buf,"No response from host");
		wn->GetIpInfo(i, ipinfo);
		if(strlen(ipinfo)==0) strcpy(ipinfo,"...");
		int avg = wn->GetAvg(i);
		const char* cl = avg < 50 ? "green" : (avg < 150 ? "yellow" : "red");
		sprintf(t_buf, "<tr><td>%d</td><td>%s</td><td>%d%%</td><td>%d</td><td>%d</td><td class=\"%s\">%d</td><td class=\"%s\">%d</td><td class=\"%s\">%d</td><td class=\"%s\">%d</td><td>%s</td></tr>\r\n",
			i+1, buf, wn->GetPercent(i), wn->GetXmit(i), wn->GetReturned(i),
			cl, wn->GetBest(i), cl, avg, cl, wn->GetWorst(i), cl, wn->GetLast(i), ipinfo);
		strcat(f_buf, t_buf);
	}
	sprintf(t_buf, "</table><p><small>Generated by WinMTR - Network Diagnostic Tool</small></p></body></html>\r\n"); strcat(f_buf, t_buf);
	CString source(f_buf);
	OpenClipboard(); EmptyClipboard();
	HGLOBAL clipbuffer = GlobalAlloc(GMEM_DDESHARE, source.GetLength()+1);
	char* buffer = (char*)GlobalLock(clipbuffer);
	strcpy(buffer, LPCSTR(source));
	GlobalUnlock(clipbuffer);
	SetClipboardData(CF_TEXT, clipbuffer);
	CloseClipboard();
}

void WinMTRDialog::OnEXPT() 
{	
	TCHAR BASED_CODE szFilter[] = _T("Text Files (*.txt)|*.txt|All Files (*.*)|*.*||");
	CFileDialog dlg(FALSE, _T("TXT"), NULL, OFN_HIDEREADONLY | OFN_EXPLORER, szFilter, this);
	if(dlg.DoModal() == IDOK) {
		if (activeSession < 0 || activeSession >= (int)sessions.size() || !sessions[activeSession]->wmtrnet) return;
		WinMTRNet* wn = sessions[activeSession]->wmtrnet;
		char buf[255], t_buf[2000], f_buf[255*1000];
		char ipinfo[255];
		int nh = wn->GetMax();
		strcpy(f_buf, "WinMTR Statistics\r\n");
		sprintf(t_buf, "Target: %s\r\nHops: %d\r\n\r\n", (LPCSTR)sessions[activeSession]->hostname, nh); strcat(f_buf, t_buf);
		sprintf(t_buf, "%-2s %-30s %6s %5s %5s %5s %5s %5s %5s %7s %s\r\n",
			"#", "Hostname", "Loss%", "Sent", "Recv", "Best", "Avrg", "Wrst", "Last", "Latency", "IP Info");
		strcat(f_buf, t_buf);
		char sep[256]; sprintf(sep, "%.90s\r\n", "------------------------------------------------------------------------------------------");
		strcat(f_buf, sep);
		for(int i=0;i<nh;i++) {
			wn->GetName(i, buf); if(strcmp(buf,"")==0) strcpy(buf,"No response from host");
			wn->GetIpInfo(i, ipinfo); if(strlen(ipinfo)==0) strcpy(ipinfo,"...");
			sprintf(t_buf, "%-2d %-30s %5d%% %4d %4d %4d %4d %4d %4d %4dms %s\r\n",
				i+1, buf, wn->GetPercent(i), wn->GetXmit(i), wn->GetReturned(i),
				wn->GetBest(i), wn->GetAvg(i), wn->GetWorst(i), wn->GetLast(i),
				wn->GetAvg(i), ipinfo);
			strcat(f_buf, t_buf);
		}
		sprintf(t_buf, "\r\nGenerated by WinMTR - Network Diagnostic Tool\r\n"); strcat(f_buf, t_buf);
		FILE *fp = fopen(dlg.GetPathName(), "wt");
		if(fp) { fprintf(fp, "%s", f_buf); fclose(fp); }
	}
}

void WinMTRDialog::OnEXPH() 
{
	TCHAR BASED_CODE szFilter[] = _T("HTML Files (*.htm, *.html)|*.htm;*.html|All Files (*.*)|*.*||");
	CFileDialog dlg(FALSE, _T("HTML"), NULL, OFN_HIDEREADONLY | OFN_EXPLORER, szFilter, this);
	if(dlg.DoModal() == IDOK) {
		if (activeSession < 0 || activeSession >= (int)sessions.size() || !sessions[activeSession]->wmtrnet) return;
		WinMTRNet* wn = sessions[activeSession]->wmtrnet;
		char buf[255], t_buf[2000], f_buf[255*1000];
		char ipinfo[255];
		int nh = wn->GetMax();
		strcpy(f_buf, "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>WinMTR Statistics</title>\r\n");
		strcat(f_buf, "<style>body{font-family:'Segoe UI',Arial,sans-serif;background:#1a1a2e;color:#e0e0e0;margin:30px}");
		strcat(f_buf, "table{border-collapse:collapse;width:100%%;margin-top:15px}th{background:#16213e;color:#4fd1c5;padding:10px;text-align:left}");
		strcat(f_buf, "td{padding:8px 10px;border-bottom:1px solid #333}");
		strcat(f_buf, "tr:hover{background:#0f3460}.g{color:#4fd1c5}.r{color:#f87171}.y{color:#fbbf24}");
		strcat(f_buf, "h1{color:#4fd1c5;margin-bottom:5px}p{margin:4px 0;color:#888}</style></head><body>\r\n");
		sprintf(t_buf, "<h1>WinMTR Statistics</h1>\r\n"); strcat(f_buf, t_buf);
		time_t now = time(NULL);
		sprintf(t_buf, "<p>Target: <strong>%s</strong> | Hops: %d</p>\r\n",
			(LPCSTR)sessions[activeSession]->hostname, nh); strcat(f_buf, t_buf);
		sprintf(t_buf, "<table><tr><th>#</th><th>Hostname</th><th>Loss%%</th><th>Sent</th><th>Recv</th><th>Best</th><th>Avrg</th><th>Worst</th><th>Last</th><th>IP Info</th></tr>\r\n"); strcat(f_buf, t_buf);
		for(int i=0;i<nh;i++) {
			wn->GetName(i, buf); if(strcmp(buf,"")==0) strcpy(buf,"No response from host");
			wn->GetIpInfo(i, ipinfo); if(strlen(ipinfo)==0) strcpy(ipinfo,"...");
			int avg = wn->GetAvg(i);
			const char* cl = avg < 50 ? "g" : (avg < 150 ? "y" : "r");
			sprintf(t_buf, "<tr><td>%d</td><td>%s</td><td>%d%%</td><td>%d</td><td>%d</td><td class=\"%s\">%d</td><td class=\"%s\">%d</td><td class=\"%s\">%d</td><td class=\"%s\">%d</td><td>%s</td></tr>\r\n",
				i+1, buf, wn->GetPercent(i), wn->GetXmit(i), wn->GetReturned(i),
				cl, wn->GetBest(i), cl, avg, cl, wn->GetWorst(i), cl, wn->GetLast(i), ipinfo);
			strcat(f_buf, t_buf);
		}
		sprintf(t_buf, "</table><p style=\"margin-top:20px\">Generated by WinMTR - Network Diagnostic Tool</p></body></html>\r\n"); strcat(f_buf, t_buf);
		FILE *fp = fopen(dlg.GetPathName(), "wt");
		if(fp) { fprintf(fp, "%s", f_buf); fclose(fp); }
	}
}

void WinMTRDialog::OnCancel() {}

//*****************************************************************************
// WinMTRDialog::DisplayRedraw
//
//*****************************************************************************
int WinMTRDialog::DisplayRedraw()
{
	if (activeSession < 0 || activeSession >= (int)sessions.size()) return 0;
	TraceSession* sess = sessions[activeSession];
	if (!sess->wmtrnet) return 0;

	char buf[255], nr_crt[255];
	int nh = sess->wmtrnet->GetMax();
	while( m_listMTR.GetItemCount() > nh ) m_listMTR.DeleteItem(m_listMTR.GetItemCount() - 1);

	for(int i=0;i <nh ; i++) {
		sess->wmtrnet->GetName(i, buf);
		if( strcmp(buf,"")==0 ) strcpy(buf,"No response from host");
		
		sprintf(nr_crt, "%d", i+1);
		if(m_listMTR.GetItemCount() <= i )
			m_listMTR.InsertItem(i, buf);
		else
			m_listMTR.SetItem(i, 0, LVIF_TEXT, buf, 0, 0, 0, 0); 
		
		m_listMTR.SetItem(i, 1, LVIF_TEXT, nr_crt, 0, 0, 0, 0); 
		sprintf(buf, "%d", sess->wmtrnet->GetPercent(i));
		m_listMTR.SetItem(i, 2, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d", sess->wmtrnet->GetXmit(i));
		m_listMTR.SetItem(i, 3, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d", sess->wmtrnet->GetReturned(i));
		m_listMTR.SetItem(i, 4, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d", sess->wmtrnet->GetBest(i));
		m_listMTR.SetItem(i, 5, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d", sess->wmtrnet->GetAvg(i));
		m_listMTR.SetItem(i, 6, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d", sess->wmtrnet->GetWorst(i));
		m_listMTR.SetItem(i, 7, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d", sess->wmtrnet->GetLast(i));
		m_listMTR.SetItem(i, 8, LVIF_TEXT, buf, 0, 0, 0, 0);
		sprintf(buf, "%d ms", sess->wmtrnet->GetAvg(i));
		m_listMTR.SetItem(i, 9, LVIF_TEXT, buf, 0, 0, 0, 0);
		sess->wmtrnet->GetIpInfo(i, buf);
		if(strlen(buf) == 0) strcpy(buf, "...");
		m_listMTR.SetItem(i, 10, LVIF_TEXT, buf, 0, 0, 0, 0);
	}

	if (m_summaryText.IsWindowVisible()) {
		DisplaySummary();
	}
	return 0;
}

//*****************************************************************************
// WinMTRDialog::DisplaySummary
//
//*****************************************************************************
int WinMTRDialog::DisplaySummary()
{
	CString allSummary;
	int totalSessionsTracing = 0;

	for (size_t s = 0; s < sessions.size(); s++) {
		if (!sessions[s]->wmtrnet || !sessions[s]->tracing) continue;
		totalSessionsTracing++;
		WinMTRNet* wn = sessions[s]->wmtrnet;

		int nh = wn->GetMax();
		if (nh == 0) continue;
		int totalSent = 0, totalRecv = 0, totalLoss = 0;
		int bestOverall = 999999, worstOverall = 0, totalAvg = 0;
		int activeHops = 0;

		for (int i = 0; i < nh; i++) {
			int sent = wn->GetXmit(i);
			if (sent > 0) {
				activeHops++;
				totalSent += sent;
				totalRecv += wn->GetReturned(i);
				totalAvg += wn->GetAvg(i);
				totalLoss += wn->GetPercent(i);
				int b = wn->GetBest(i);
				int w = wn->GetWorst(i);
				if (b > 0 && b < bestOverall) bestOverall = b;
				if (w > worstOverall) worstOverall = w;
			}
		}

		int avgLoss = activeHops > 0 ? totalLoss / activeHops : 0;
		int avgLatency = activeHops > 0 ? totalAvg / activeHops : 0;

		char buf[512];
		sprintf(buf, "[%s] Hops:%d | Loss:%d%% | Best:%dms | Avg:%dms | Worst:%dms\r\n\r\n",
			(LPCSTR)sessions[s]->hostname, nh, avgLoss,
			bestOverall < 999999 ? bestOverall : 0,
			avgLatency, worstOverall);
		allSummary += buf;
	}

	if (totalSessionsTracing == 0) {
		allSummary = "No active traces.\r\n\r\nClick '+' to add a new host tab, then enter a hostname and click Start.";
	}

	m_summaryText.SetWindowText(allSummary);
	return 0;
}

//*****************************************************************************
// WinMTRDialog::InitMTRNet
//
//*****************************************************************************
int WinMTRDialog::InitMTRNet()
{
	char strtmp[255];
	char *Hostname = strtmp;
	char buf[255];
	struct hostent *host;
	m_comboHost.GetWindowText(strtmp, 255);
	if (Hostname == NULL) Hostname = "localhost";
   
	int isIP=1;
	char *t = Hostname;
	while(*t) {
		if(!isdigit(*t) && *t!='.') { isIP=0; break; }
		t++;
	}
	if(!isIP) {
		sprintf(buf, "Resolving host %s...", strtmp);
		statusBar.SetPaneText(0, buf);
		host = gethostbyname(Hostname);
		if(host == NULL) {
			statusBar.SetPaneText(0, CString((LPCSTR)IDS_STRING_SB_NAME));
			AfxMessageBox("Unable to resolve hostname.");
			return 0;
		}
	}
	return 1;
}

//*****************************************************************************
// PingThread
//
//*****************************************************************************
void PingThread(void *p)
{
	PingThreadData *ptd = (PingThreadData *)p;
	WinMTRDialog *wmtrdlg = ptd->dialog;
	int sessionIdx = ptd->sessionIdx;

	struct hostent *host;
	char *Hostname = ptd->hostname;
	int traddr;

	int isIP = 1;
	char *t = Hostname;
	while (*t) {
		if (!isdigit(*t) && *t != '.') { isIP = 0; break; }
		t++;
	}

	if (!isIP) {
		host = gethostbyname(Hostname);
		traddr = *(int *)host->h_addr;
	} else {
		traddr = inet_addr(Hostname);
	}

	// Start trace on the specific session using its own mutex
	if (sessionIdx >= 0 && sessionIdx < (int)wmtrdlg->sessions.size()) {
		TraceSession* sess = wmtrdlg->sessions[sessionIdx];
		WaitForSingleObject(sess->traceMutex, INFINITE);
		if (sess->wmtrnet) {
			sess->wmtrnet->DoTrace(traddr);
		}
		ReleaseMutex(sess->traceMutex);
	}

	delete ptd;
	_endthread();
}


void WinMTRDialog::OnCbnSelchangeComboHost() {}
void WinMTRDialog::ClearHistory()
{
	HKEY hKey; DWORD tmp_dword; LONG r; char key_name[20];
	r = RegOpenKeyEx(HKEY_CURRENT_USER, "Software", 0, KEY_ALL_ACCESS, &hKey);
	r = RegOpenKeyEx(hKey, "WinMTR", 0, KEY_ALL_ACCESS, &hKey);
	r = RegOpenKeyEx(hKey, "LRU", 0, KEY_ALL_ACCESS, &hKey);
	for(int i=0;i<=nrLRU;i++) { sprintf(key_name,"Host%d",i); RegDeleteValue(hKey,key_name); }
	nrLRU = 0; tmp_dword = nrLRU;
	RegSetValueEx(hKey,"NrLRU", 0, REG_DWORD, (const unsigned char *)&tmp_dword, sizeof(DWORD));
	RegCloseKey(hKey);
	m_comboHost.Clear(); m_comboHost.ResetContent();
	m_comboHost.AddString(CString((LPCSTR)IDS_STRING_CLEAR_HISTORY));
}
void WinMTRDialog::OnCbnSelendokComboHost() {}
void WinMTRDialog::OnCbnCloseupComboHost() {
	if(m_comboHost.GetCurSel() == m_comboHost.GetCount() - 1) ClearHistory();
}

void WinMTRDialog::EnableIdleControls()
{
	m_buttonStart.SetWindowText("Start");
	m_buttonStart.EnableWindow(TRUE);
	m_comboHost.EnableWindow(TRUE);
	m_buttonOptions.EnableWindow(TRUE);
	m_tabCtrl.EnableWindow(TRUE);
	m_comboHost.SetFocus();
}

void WinMTRDialog::Transit(STATES new_state)
{
	switch(new_state) {
		case IDLE:
			switch (state) {
				case STOPPING: transition = STOPPING_TO_IDLE; break;
				case IDLE: transition = IDLE_TO_IDLE; break;
				default: TRACE_MSG("Received state IDLE after " << state); return;
			}
			state = IDLE;
		break;
		case TRACING:
			switch (state) {
				case IDLE: transition = IDLE_TO_TRACING; break;
				case TRACING: transition = TRACING_TO_TRACING; break;
				default: TRACE_MSG("Received state TRACING after " << state); return;
			}
			state = TRACING;
		break;
		case STOPPING:
			switch (state) {
				case STOPPING: transition = STOPPING_TO_STOPPING; break;
				case TRACING: transition = TRACING_TO_STOPPING; break;
				default: TRACE_MSG("Received state STOPPING after " << state); return;
			}
			state = STOPPING;
		break;
		case EXIT:
			switch (state) {
				case IDLE: transition = IDLE_TO_EXIT; break;
				case STOPPING: transition = STOPPING_TO_EXIT; break;
				case TRACING: transition = TRACING_TO_EXIT; break;
				case EXIT: break;
				default: TRACE_MSG("Received state EXIT after " << state); return;
			}
			state = EXIT;
		break;
		default: TRACE_MSG("Received state " << state);
	}

	switch(transition) {
		case IDLE_TO_TRACING:
			m_buttonStart.EnableWindow(FALSE);
			m_buttonStart.SetWindowText("Stop");
			m_comboHost.EnableWindow(FALSE);
			m_buttonOptions.EnableWindow(FALSE);
			m_tabCtrl.EnableWindow(TRUE);
			statusBar.SetPaneText(0, "Double click on host name for more information.");
			m_buttonStart.EnableWindow(TRUE);
		break;
		case IDLE_TO_IDLE: break;
		case STOPPING_TO_IDLE:
			m_buttonStart.EnableWindow(TRUE);
			statusBar.SetPaneText(0, CString((LPCSTR)IDS_STRING_SB_NAME));
			m_buttonStart.SetWindowText("Start");
			m_comboHost.EnableWindow(TRUE);
			m_buttonOptions.EnableWindow(TRUE);
			m_tabCtrl.EnableWindow(TRUE);
			m_comboHost.SetFocus();
		break;
		case STOPPING_TO_STOPPING: DisplayRedraw(); break;
		case TRACING_TO_TRACING: DisplayRedraw(); break;
		case TRACING_TO_STOPPING:
			m_buttonStart.EnableWindow(FALSE);
			m_comboHost.EnableWindow(FALSE);
			m_buttonOptions.EnableWindow(FALSE);
			m_tabCtrl.EnableWindow(TRUE);
			if (activeSession >= 0 && activeSession < (int)sessions.size() && sessions[activeSession]->wmtrnet) {
				sessions[activeSession]->wmtrnet->StopTrace();
				sessions[activeSession]->tracing = false;
			}
			statusBar.SetPaneText(0, "Waiting for last packets in order to stop trace ...");
			DisplayRedraw();
		break;
		case IDLE_TO_EXIT:
		case TRACING_TO_EXIT:
		case STOPPING_TO_EXIT:
			m_buttonStart.EnableWindow(FALSE);
			m_comboHost.EnableWindow(FALSE);
			m_buttonOptions.EnableWindow(FALSE);
			if (activeSession >= 0 && activeSession < (int)sessions.size() && sessions[activeSession]->wmtrnet) {
				sessions[activeSession]->wmtrnet->StopTrace();
			}
		break;
		default: TRACE_MSG("Unknown transition " << transition);
	}
}


void WinMTRDialog::OnTimer(UINT_PTR nIDEvent)
{
	static unsigned int call_count = 0;
	call_count += 1;

	if(state == EXIT && WaitForSingleObject(traceThreadMutex, 0) == WAIT_OBJECT_0) {
		ReleaseMutex(traceThreadMutex);
		OnOK();
	}

	// Check all active sessions for completion
	bool allIdle = true;
	for (size_t s = 0; s < sessions.size(); s++) {
		if (sessions[s]->tracing) {
			allIdle = false;
			break;
		}
	}

	if (state == TRACING && allIdle) {
		Transit(IDLE);
	} else if (call_count % 3 == 0) {
		// Refresh the display for the active session every 300ms
		if (activeSession >= 0 && activeSession < (int)sessions.size() && sessions[activeSession]->tracing) {
			if (!sessions[activeSession]->wmtrnet) return;
			// Just update the numbers, don't do full Transit
			char buf[255], nr_crt[255];
			WinMTRNet* wn = sessions[activeSession]->wmtrnet;
			int nh = wn->GetMax();
			while( m_listMTR.GetItemCount() > nh ) m_listMTR.DeleteItem(m_listMTR.GetItemCount() - 1);
			bool tableVisible = m_listMTR.IsWindowVisible();

			for(int i=0;i <nh ; i++) {
				wn->GetName(i, buf);
				if( strcmp(buf,"")==0 ) strcpy(buf,"No response from host");
				sprintf(nr_crt, "%d", i+1);
				if(m_listMTR.GetItemCount() <= i )
					m_listMTR.InsertItem(i, buf);
				else
					m_listMTR.SetItem(i, 0, LVIF_TEXT, buf, 0, 0, 0, 0); 
				
				m_listMTR.SetItem(i, 1, LVIF_TEXT, nr_crt, 0, 0, 0, 0); 
				sprintf(buf, "%d", wn->GetPercent(i));
				m_listMTR.SetItem(i, 2, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d", wn->GetXmit(i));
				m_listMTR.SetItem(i, 3, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d", wn->GetReturned(i));
				m_listMTR.SetItem(i, 4, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d", wn->GetBest(i));
				m_listMTR.SetItem(i, 5, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d", wn->GetAvg(i));
				m_listMTR.SetItem(i, 6, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d", wn->GetWorst(i));
				m_listMTR.SetItem(i, 7, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d", wn->GetLast(i));
				m_listMTR.SetItem(i, 8, LVIF_TEXT, buf, 0, 0, 0, 0);
				sprintf(buf, "%d ms", wn->GetAvg(i));
				m_listMTR.SetItem(i, 9, LVIF_TEXT, buf, 0, 0, 0, 0);
				wn->GetIpInfo(i, buf);
				if(strlen(buf) == 0) strcpy(buf, "...");
				m_listMTR.SetItem(i, 10, LVIF_TEXT, buf, 0, 0, 0, 0);
			}
			if (m_summaryText.IsWindowVisible()) DisplaySummary();
		}
	}

	// Handle tab clicks
	static int lastTabSelection = 0;
	int curSel = -1;
	if (::IsWindow(m_tabCtrl.m_hWnd)) {
		curSel = m_tabCtrl.GetCurSel();
	}
	
	if (curSel != lastTabSelection && curSel >= 0 && ::IsWindow(m_tabCtrl.m_hWnd)) {
		lastTabSelection = curSel;
		int tabCount = m_tabCtrl.GetItemCount();

		if (curSel == tabCount - 1) {
			// "+" tab clicked - add new session
			if (sessions.size() >= MAX_SESSIONS) {
				AfxMessageBox("Maximum 10 concurrent traces reached.");
				m_tabCtrl.SetCurSel(activeSession);
				lastTabSelection = activeSession;
				return;
			}
			TraceSession* ns = new TraceSession;
			ns->hostname = "New Host";
			ns->wmtrnet = NULL;
			ns->tracing = false;
			ns->traceMutex = CreateMutex(NULL, FALSE, NULL);
			ns->tabIndex = (int)sessions.size();
			sessions.push_back(ns);
			activeSession = (int)sessions.size() - 1;
			RefreshTabs();
			m_tabCtrl.SetCurSel(activeSession);
			lastTabSelection = activeSession;
			m_listMTR.DeleteAllItems();
			m_summaryText.ShowWindow(SW_HIDE);
			m_listMTR.ShowWindow(SW_SHOW);
			EnableIdleControls();
		} else if (curSel >= 0 && curSel < (int)sessions.size()) {
			// Switch to existing session tab
			activeSession = curSel;
			m_listMTR.DeleteAllItems();
			m_summaryText.ShowWindow(SW_HIDE);
			m_listMTR.ShowWindow(SW_SHOW);

			// Update host text box to show this session's hostname
			m_comboHost.SetWindowText(sessions[activeSession]->hostname);

			if (sessions[activeSession]->tracing) {
				// Running trace - show stop button, disable host input
				m_buttonStart.SetWindowText("Stop");
				m_buttonStart.EnableWindow(TRUE);
				m_comboHost.EnableWindow(FALSE);
				m_buttonOptions.EnableWindow(FALSE);
				DisplayRedraw();
			} else {
				// Idle session - show start button, enable inputs
				EnableIdleControls();
			}
		}
	}

	CDialog::OnTimer(nIDEvent);
}

void WinMTRDialog::OnClose()
{
	if (AfxMessageBox("Exit WinMTR? Any running traces will be stopped.", MB_YESNO | MB_ICONQUESTION) == IDYES) {
		Transit(EXIT);
	}
}

void WinMTRDialog::OnBnClickedCancel()
{
	if (AfxMessageBox("Exit WinMTR? Any running traces will be stopped.", MB_YESNO | MB_ICONQUESTION) == IDYES) {
		Transit(EXIT);
	}
}
