//*****************************************************************************
// FILE:            WinMTRDialog.h
//
//
// DESCRIPTION:
//   
//
// NOTES:
//    
//
//*****************************************************************************

#ifndef WINMTRDIALOG_H_
#define WINMTRDIALOG_H_

#define WINMTR_DIALOG_TIMER 100
#define MAX_SESSIONS 10

#include <vector>
#include "WinMTRStatusBar.h"
#include "WinMTRNet.h"

#define MAX_HISTORY_SAMPLES 7200

struct HistorySample {
	time_t timestamp;
	int avgLatency;
	int packetLoss;
};

struct TraceSession {
	CString hostname;
	WinMTRNet* wmtrnet;
	bool tracing;
	HANDLE traceMutex;
	int tabIndex;
	HistorySample* history;
	int historyPos;
	int historyCount;
	time_t traceStartTime;
};

//*****************************************************************************
// CLASS:  WinMTRDialog
//
//
//*****************************************************************************

class WinMTRDialog : public CDialog
{
public:
	WinMTRDialog(CWnd* pParent = NULL);
	~WinMTRDialog();

	enum { IDD = IDD_WINMTR_DIALOG };

	afx_msg BOOL InitRegistry();

	WinMTRStatusBar	statusBar;

	enum STATES {
		IDLE,
		TRACING,
		STOPPING,
		EXIT
	};

	enum STATE_TRANSITIONS {
		IDLE_TO_IDLE,
		IDLE_TO_TRACING,
		IDLE_TO_EXIT,
		TRACING_TO_TRACING,
		IDLE_TO_START_NEW,
		STOPPING_TO_IDLE,
		STOPPING_TO_STOPPING,
		STOPPING_TO_EXIT,
		TRACING_TO_STOPPING,
		TRACING_TO_EXIT
	};

	CButton	m_buttonOptions;
	CButton	m_buttonExit;
	CButton	m_buttonStart;
	CComboBox m_comboHost;
	CListCtrl	m_listMTR;

	CStatic	m_staticS;
	CStatic	m_staticJ;

	CButton	m_buttonExpT;
	CButton	m_buttonExpH;

	CTabCtrl m_tabCtrl;
	CStatic m_summaryText;
	CStatic m_graphFrame;
	CButton m_btn5min, m_btn15min, m_btn1hr, m_btn12hr, m_btn24hr;
	
	int graphRange; // seconds to display
	bool showGraph;

	std::vector<TraceSession*> sessions;
	int activeSession;
	
	int InitMTRNet();
	int DisplaySummary();

	int DisplayRedraw();
	void Transit(STATES new_state);

	STATES				state;
	STATE_TRANSITIONS	transition;
	double				interval;
	bool				hasIntervalFromCmdLine;
	int					pingsize;
	bool				hasPingsizeFromCmdLine;
	int					maxLRU;
	bool				hasMaxLRUFromCmdLine;
	int					nrLRU;
	BOOL				useDNS;
	bool				hasUseDNSFromCmdLine;

	void SetHostName(const char *host);
	void SetInterval(float i);
	void SetPingSize(int ps);
	void SetMaxLRU(int mlru);
	void SetUseDNS(BOOL udns);

	HANDLE				traceThreadMutex;  // guards tab switching during trace

protected:
	virtual void DoDataExchange(CDataExchange* pDX);

	int m_autostart;
	char msz_defaulthostname[1000];
	
	HICON m_hIcon;

	virtual BOOL OnInitDialog();
	afx_msg void OnPaint();
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnSizing(UINT, LPRECT); 
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnRestart();
	afx_msg void OnOptions();
	virtual void OnCancel();

	afx_msg void OnCTTC();
	afx_msg void OnCHTC();
	afx_msg void OnEXPT();
	afx_msg void OnEXPH();

	afx_msg void OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult);
	
	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnCbnSelchangeComboHost();
	afx_msg void OnCbnSelendokComboHost();
private:
	void ClearHistory();
	void RefreshTabs();
	void StartTraceForSession(int idx, const char* hostname);
	void CloseSession(int idx);
	void EnableIdleControls();
	void RecordHistory();
	void DrawGraph(CDC* pDC, CRect& rc);
public:
	afx_msg void OnCbnCloseupComboHost();
	afx_msg void OnBtn5min();
	afx_msg void OnBtn15min();
	afx_msg void OnBtn1hr();
	afx_msg void OnBtn12hr();
	afx_msg void OnBtn24hr();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnClose();
	afx_msg void OnBnClickedCancel();
};

#endif // ifndef WINMTRDIALOG_H_