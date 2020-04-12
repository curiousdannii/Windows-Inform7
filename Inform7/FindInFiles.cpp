#include "stdafx.h"
#include "FindInFiles.h"
#include "ProjectFrame.h"
#include "TextFormat.h"
#include "OSLayer.h"
#include "resource.h"

#include "Platform.h"
#include "Scintilla.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define FIND_HISTORY_LENGTH 30

// Implementation of IEnumString for auto-completion
class FindEnumString : public CCmdTarget
{
public:
  FindEnumString(FindInFiles* parent) : m_parent(parent), m_iter(0)
  {
  }

  DECLARE_INTERFACE_MAP()

  BEGIN_INTERFACE_PART(EnumString, IEnumString)
    HRESULT Next(ULONG, LPOLESTR*, ULONG*);
    HRESULT Skip(ULONG);
    HRESULT Reset(void);
    HRESULT Clone(IEnumString**);
  END_INTERFACE_PART(EnumString)

private:
  FindInFiles* m_parent;
  int m_iter;
};

BEGIN_INTERFACE_MAP(FindEnumString, CCmdTarget)
  INTERFACE_PART(FindEnumString, IID_IEnumString, EnumString)
END_INTERFACE_MAP()

STDMETHODIMP_(ULONG) FindEnumString::XEnumString::AddRef()
{
  METHOD_PROLOGUE(FindEnumString, EnumString)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) FindEnumString::XEnumString::Release()
{
  METHOD_PROLOGUE(FindEnumString, EnumString)
  return pThis->ExternalRelease();
}

STDMETHODIMP FindEnumString::XEnumString::QueryInterface(REFIID iid, LPVOID* ppvObj)
{
  METHOD_PROLOGUE(FindEnumString, EnumString)
  return (HRESULT)pThis->InternalQueryInterface(&iid,ppvObj);
}

HRESULT FindEnumString::XEnumString::Next(ULONG count, LPOLESTR* strs, ULONG* pDone)
{
  METHOD_PROLOGUE(FindEnumString, EnumString)

  ULONG done = 0;
  if (pDone)
    *pDone = done;

  for (ULONG i = 0; i < count; i++)
  {
    LPCWSTR str = pThis->m_parent->GetAutoComplete(pThis->m_iter);
    if (str)
    {
      strs[done] = (LPOLESTR)::CoTaskMemAlloc(sizeof(WCHAR) * (wcslen(str)+1));
      wcscpy(strs[done],str);

      pThis->m_iter++;
      done++;
      if (pDone)
        *pDone = done;
    }
    else
      return S_FALSE;
  }
  return S_OK;
}

HRESULT FindEnumString::XEnumString::Skip(ULONG count)
{
  METHOD_PROLOGUE(FindEnumString, EnumString)
  pThis->m_iter += count;
  return S_OK;
}

HRESULT FindEnumString::XEnumString::Reset(void)
{
  METHOD_PROLOGUE(FindEnumString, EnumString)
  pThis->m_iter = 0;
  return S_OK;
}

HRESULT FindEnumString::XEnumString::Clone(IEnumString**)
{
  return E_FAIL;
}

// The one and only FindInFiles object
FindInFiles theFinder;

BEGIN_MESSAGE_MAP(FindInFiles, I7BaseDialog)
  ON_WM_CLOSE()
  ON_WM_GETMINMAXINFO()
  ON_WM_SIZE()
  ON_BN_CLICKED(IDC_FIND_ALL, OnFindAll)
  ON_NOTIFY(NM_CUSTOMDRAW, IDC_RESULTS, OnResultsDraw)
END_MESSAGE_MAP()

FindInFiles::FindInFiles() : I7BaseDialog(IDD_FIND_FILES)
{
  m_project = NULL;

  m_lookSource = TRUE;
  m_lookExts = TRUE;
  m_lookDocPhrases = TRUE;
  m_lookDocMain = TRUE;
  m_lookDocExamples = TRUE;

  m_ignoreCase = TRUE;
  m_findRule = 0;
}

void FindInFiles::Show(ProjectFrame* project)
{
  if (GetSafeHwnd() == 0)
  {
    Create(m_lpszTemplateName,CWnd::GetDesktopWindow());
    theApp.SetIcon(this);

    bool placementSet = false;
    CRegKey registryKey;
    if (registryKey.Create(HKEY_CURRENT_USER,REGISTRY_PATH_WINDOW) == ERROR_SUCCESS)
    {
      // Restore the window state
      WINDOWPLACEMENT place;
      ULONG len = sizeof WINDOWPLACEMENT;
      if (registryKey.QueryBinaryValue("Find in Files Placement",&place,&len) == ERROR_SUCCESS)
        SetWindowPlacement(&place);
      placementSet = true;
    }
    if (!placementSet)
      CenterWindow(project);
  }

  if (GetSafeHwnd() != 0)
  {
    m_project = project;
    ShowWindow(SW_SHOW);
    GetDlgItem(IDC_FIND)->SetFocus();
  }
}

void FindInFiles::Hide(ProjectFrame* project)
{
  if (m_project == project)
  {
    ShowWindow(SW_HIDE);
    m_project = NULL;
  }
}

void FindInFiles::FindInSource(LPCWSTR text)
{
  UpdateData(TRUE);
  m_lookSource = TRUE;
  m_lookExts = FALSE;
  m_lookDocPhrases = FALSE;
  m_lookDocMain = FALSE;
  m_lookDocExamples = FALSE;
  m_ignoreCase = TRUE;
  m_findRule = 0;
  m_findText = text;
  UpdateData(FALSE);
  PostMessage(WM_COMMAND,IDC_FIND_ALL);
}

void FindInFiles::FindInDocs(LPCWSTR text)
{
  UpdateData(TRUE);
  m_lookSource = FALSE;
  m_lookExts = FALSE;
  m_lookDocPhrases = TRUE;
  m_lookDocMain = TRUE;
  m_lookDocExamples = TRUE;
  m_ignoreCase = TRUE;
  m_findRule = 0;
  m_findText = text;
  UpdateData(FALSE);
  PostMessage(WM_COMMAND,IDC_FIND_ALL);
}

void FindInFiles::InitInstance(void)
{
  try
  {
    // Read the serialized auto complete history from the registry
    CRegKey registryKey;
    if (registryKey.Open(HKEY_CURRENT_USER,REGISTRY_PATH_WINDOW,KEY_READ) == ERROR_SUCCESS)
    {
      ULONG historyLen = 0;
      if (registryKey.QueryBinaryValue("Find in Files History",NULL,&historyLen) == ERROR_SUCCESS)
      {
        BYTE* historyPtr = new BYTE[historyLen];
        if (registryKey.QueryBinaryValue("Find in Files History",historyPtr,&historyLen) == ERROR_SUCCESS)
        {
          CMemFile historyFile(historyPtr,historyLen);
          CArchive historyArchive(&historyFile,CArchive::load);
          INT_PTR historyCount = 0;
          historyArchive >> historyCount;
          for (INT_PTR i = 0; i < historyCount; i++)
          {
            CStringW historyItem;
            historyArchive >> historyItem;
            m_findHistory.AddTail(historyItem);
          }
        }
      }
    }
  }
  catch (CException* ex)
  {
    ex->Delete();
  }
}

void FindInFiles::ExitInstance(void)
{
  CRegKey registryKey;
  if (registryKey.Open(HKEY_CURRENT_USER,REGISTRY_PATH_WINDOW,KEY_WRITE) == ERROR_SUCCESS)
  {
    // Save the window state
    if (GetSafeHwnd() != 0)
    {
      WINDOWPLACEMENT place;
      place.length = sizeof place;
      GetWindowPlacement(&place);
      registryKey.SetBinaryValue("Find in Files Placement",&place,sizeof WINDOWPLACEMENT);
    }

    // Save the auto complete history
    try
    {
      if (m_findHistory.GetCount() > 0)
      {
        // Write the list of the auto complete history to a memory file
        CMemFile historyFile;
        CArchive historyArchive(&historyFile,CArchive::store);
        historyArchive << m_findHistory.GetCount();
        POSITION pos = m_findHistory.GetHeadPosition();
        for (INT_PTR i = 0; i < m_findHistory.GetCount(); i++)
          historyArchive << m_findHistory.GetNext(pos);
        historyArchive.Close();
 
        // Write the above buffer to the registry
        ULONG historyLen = (ULONG)historyFile.GetLength();
        void* historyPtr = historyFile.Detach();
        registryKey.SetBinaryValue("Find in Files History",historyPtr,historyLen);
        free(historyPtr);
      }
    }
    catch (CException* ex)
    {
      ex->Delete();
    }
  }

  // Close the window
  if (GetSafeHwnd() != 0)
    DestroyWindow();
  m_project = NULL;
}

LPCWSTR FindInFiles::GetAutoComplete(int index)
{
  if (index < m_findHistory.GetSize())
  {
    POSITION pos = m_findHistory.FindIndex(index);
    if (pos != NULL)
      return m_findHistory.GetAt(pos).GetString();
  }
  return NULL;
}

void FindInFiles::DoDataExchange(CDataExchange* pDX)
{
  I7BaseDialog::DoDataExchange(pDX);
  DDX_Control(pDX, IDC_FIND, m_find);
  DDX_TextW(pDX, IDC_FIND, m_findText);
  DDX_Check(pDX,IDC_LOOK_SOURCE,m_lookSource);
  DDX_Check(pDX,IDC_LOOK_EXTENSIONS,m_lookExts);
  DDX_Check(pDX,IDC_LOOK_DOC_PHRASES,m_lookDocPhrases);
  DDX_Check(pDX,IDC_LOOK_DOC_MAIN,m_lookDocMain);
  DDX_Check(pDX,IDC_LOOK_DOC_EXAMPLES,m_lookDocExamples);
  DDX_Check(pDX,IDC_IGNORE_CASE,m_ignoreCase);
  DDX_CBIndex(pDX,IDC_FIND_RULE,m_findRule);
  DDX_Control(pDX, IDC_RESULTS, m_resultsList);
}

BOOL FindInFiles::OnInitDialog()
{
  if (I7BaseDialog::OnInitDialog())
  {
    // Initialize auto-completion for the find string
    if (FAILED(m_findAutoComplete.CoCreateInstance(CLSID_AutoComplete)))
      return FALSE;
    FindEnumString* fes = new FindEnumString(this);
    CComQIPtr<IEnumString> ies(fes->GetInterface(&IID_IEnumString));
    m_findAutoComplete->Init(m_find.GetSafeHwnd(),ies,NULL,NULL);
    m_findAutoComplete->SetOptions(ACO_AUTOSUGGEST);
    fes->ExternalRelease();

    m_resultsList.SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
    m_resultsList.InsertColumn(0,"Result");
    m_resultsList.InsertColumn(1,"Document");
    m_resultsList.InsertColumn(2,"Type");

    CRect windowRect, resultsRect;
    GetWindowRect(windowRect);
    m_minSize = windowRect.Size();
    m_resultsList.GetWindowRect(resultsRect);
    m_resultsBottomRight = windowRect.BottomRight() - resultsRect.BottomRight();
    SetResultsWidths();
    return TRUE;
  }
  return FALSE;
}

void FindInFiles::OnCancel()
{
  SendMessage(WM_CLOSE);
}

void FindInFiles::OnClose()
{
  ShowWindow(SW_HIDE);
  if (m_project)
  {
    m_project->SetActiveWindow();
    m_project = NULL;
  }
}

void FindInFiles::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
  I7BaseDialog::OnGetMinMaxInfo(lpMMI);
  lpMMI->ptMinTrackSize.x = m_minSize.cx;
  lpMMI->ptMinTrackSize.y = m_minSize.cy;
}

void FindInFiles::OnSize(UINT nType, int cx, int cy)
{
  I7BaseDialog::OnSize(nType,cx,cy);

  if (m_resultsList.GetSafeHwnd() != 0)
  {
    CRect windowRect, resultsRect;
    GetWindowRect(windowRect);
    m_resultsList.GetWindowRect(resultsRect);
    resultsRect.right = windowRect.right - m_resultsBottomRight.cx;
    resultsRect.bottom = windowRect.bottom - m_resultsBottomRight.cy;
    ScreenToClient(resultsRect);
    m_resultsList.MoveWindow(resultsRect);
    SetResultsWidths();
  }
}

void FindInFiles::OnFindAll()
{
  ASSERT(m_project);
  if (m_project == NULL)
    return;

  CWaitCursor wc;
  UpdateData(TRUE);
  if (m_findText.IsEmpty())
    return;

  // Update the find history
  POSITION pos = m_findHistory.Find(m_findText);
  if (pos == NULL)
    m_findHistory.AddTail(m_findText);
  else if (pos != m_findHistory.GetTailPosition())
  {
    m_findHistory.RemoveAt(pos);
    m_findHistory.AddTail(m_findText);
  }
  while (m_findHistory.GetSize() > FIND_HISTORY_LENGTH)
    m_findHistory.RemoveHead();

  // Update the find history dropdown
  CComQIPtr<IAutoCompleteDropDown> dropdown(m_findAutoComplete);
  if (dropdown)
    dropdown->ResetEnumerator();

  // Search for the text
  m_results.clear();
  if (m_lookSource)
    Find(m_project->SendMessage(WM_SOURCEEDITPTR));

  // Update the results
  m_resultsList.DeleteAllItems();
  for (int i = 0; i < (int)m_results.size(); i++)
  {
    m_resultsList.InsertItem(i,"");
    m_resultsList.SetItemData(i,(DWORD_PTR)(LPCWSTR)m_results[i].context);
    m_resultsList.SetItemText(i,1,m_results[i].sourceDocument);
    m_resultsList.SetItemText(i,2,m_results[i].sourceType);
  }
  SetResultsWidths();
}

void FindInFiles::OnResultsDraw(NMHDR* pNotifyStruct, LRESULT* result)
{
  // Default to letting Windows draw the control
  *result = CDRF_DODEFAULT;

  // Work out where we are in the drawing process
  NMLVCUSTOMDRAW* custom = (NMLVCUSTOMDRAW*)pNotifyStruct;
  switch (custom->nmcd.dwDrawStage)
  {
  case CDDS_PREPAINT:
    // Tell us when an item is drawn
    *result = CDRF_NOTIFYITEMDRAW;
    break;
  case CDDS_ITEMPREPAINT:
    // Tell us when a sub-item is drawn
    *result = CDRF_NOTIFYSUBITEMDRAW;
    break;
  case CDDS_ITEMPREPAINT|CDDS_SUBITEM:
  {
    // Work out the background colour
    int item = (int)custom->nmcd.dwItemSpec;
    COLORREF backColour = theApp.GetColour(m_results[item].colour);
    if ((item % 2) != 0)
      backColour = Darken(backColour);

    // Get if the item is selected
    bool selected = false;
    if (GetFocus() == &m_resultsList)
    {
      if (item == m_resultsList.GetNextItem(-1,LVNI_SELECTED))
        selected = true;
    }

    // Get the bounding rectangle for drawing the text
    CRect rect;
    m_resultsList.GetSubItemRect(item,custom->iSubItem,LVIR_LABEL,rect);
    CRect textRect = rect;
    textRect.DeflateRect(2,0);

    // Set up the device context
    HANDLE pb = 0;
    CDC* dc = theOS.BeginBufferedPaint(custom->nmcd.hdc,rect,BPBF_COMPATIBLEBITMAP,&pb);
    dc->SetTextColor(::GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    dc->SetBkMode(TRANSPARENT);
    CFont* oldFont = dc->SelectObject(m_resultsList.GetFont());

    // Draw the background
    dc->FillSolidRect(rect,selected ? ::GetSysColor(COLOR_HIGHLIGHT) : backColour);

    // Special case painting of the first column
    if (custom->iSubItem == 0)
    {
      // Get the item text
      CStringW text = (LPCWSTR)m_resultsList.GetItemData(item);

      // Create a bold font
      LOGFONT logFont;
      m_resultsList.GetFont()->GetLogFont(&logFont);
      logFont.lfWeight = FW_BOLD;
      CFont boldFont;
      boldFont.CreateFontIndirect(&logFont);

      // Get position information on the text
      int high1 = m_results[item].inContext.cpMin;
      int high2 = m_results[item].inContext.cpMax;

      // Draw the text
      DrawText(dc,text.GetString(),high1,textRect,DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      dc->SelectObject(&boldFont);
      DrawText(dc,text.GetString()+high1,high2-high1,textRect,DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
      dc->SelectObject(m_resultsList.GetFont());
      DrawText(dc,text.GetString()+high2,text.GetLength()-high2,textRect,
        DT_VCENTER|DT_SINGLELINE|DT_WORD_ELLIPSIS|DT_NOPREFIX);
    }
    else
    {
      CString text = m_resultsList.GetItemText(item,custom->iSubItem);
      dc->DrawText(text,textRect,DT_VCENTER|DT_SINGLELINE|DT_WORD_ELLIPSIS|DT_NOPREFIX);
    }

    dc->SelectObject(oldFont);
    theOS.EndBufferedPaint(pb,TRUE);
    *result = CDRF_SKIPDEFAULT;
  }
  break;
  }
}

void FindInFiles::Find(LONG_PTR editPtr)
{
  int len = (int)CallEdit(editPtr,SCI_GETLENGTH);
  TextToFind find;
  find.chrg.cpMin = 0;
  find.chrg.cpMax = len;
  CString textUtf = TextFormat::UnicodeToUTF8(m_findText);
  find.lpstrText = (char*)(LPCSTR)textUtf;

  while (true)
  {
    // Search for the text
    if (CallEdit(editPtr,SCI_FINDTEXT,0,(sptr_t)&find) == -1)
      return;

    // Get the surrounding text as context
    CStringW leading = GetTextRange(editPtr,find.chrgText.cpMin-4,find.chrgText.cpMin,len);
    CStringW match = GetTextRange(editPtr,find.chrgText.cpMin,find.chrgText.cpMax,len);
    CStringW trailing = GetTextRange(editPtr,find.chrgText.cpMax,find.chrgText.cpMax+32,len);
    CStringW context = leading + match + trailing;
    context.Replace(L'\n',L' ');
    context.Replace(L'\r',L' ');
    context.Replace(L'\t',L' ');

    // Store the found result
    FindResult result;
    result.context = context;
    result.inContext.cpMin = leading.GetLength();
    result.inContext.cpMax = leading.GetLength() + match.GetLength();
    result.sourceDocument = m_project->GetDisplayName(false);
    result.sourceType = "Source";
    result.inSource.cpMin = find.chrgText.cpMin;
    result.inSource.cpMax = find.chrgText.cpMax;
    m_results.push_back(result);

    // Look for the next match
    find.chrg.cpMin = find.chrgText.cpMax;
  }
}

extern "C" sptr_t __stdcall Scintilla_DirectFunction(sptr_t, UINT, uptr_t, sptr_t);

LONG_PTR FindInFiles::CallEdit(LONG_PTR editPtr, UINT msg, DWORD wp, LONG_PTR lp)
{
  return Scintilla_DirectFunction(editPtr,msg,wp,lp);
}

CStringW FindInFiles::GetTextRange(LONG_PTR editPtr, int cpMin, int cpMax, int len)
{
  if (cpMin < 0)
    cpMin = 0;
  if ((len >= 0) && (cpMax > len))
    cpMax = len;

  TextRange range;
  range.chrg.cpMin = cpMin;
  range.chrg.cpMax = cpMax;

  CString utfText;
  range.lpstrText = utfText.GetBufferSetLength(cpMax-cpMin+1);
  CallEdit(editPtr,SCI_GETTEXTRANGE,0,(sptr_t)&range);
  utfText.ReleaseBuffer();
  return TextFormat::UTF8ToUnicode(utfText);
}

void FindInFiles::DrawText(CDC* dc, LPCWSTR text, int length, CRect& rect, UINT format)
{
  if (length > 0)
  {
    theOS.DrawText(dc,text,length,rect,format|DT_SINGLELINE);

    CRect measure(rect);
    theOS.DrawText(dc,text,length,measure,format|DT_SINGLELINE|DT_CALCRECT);
    rect.left += measure.Width();
  }
}

COLORREF FindInFiles::Darken(COLORREF colour)
{
  BYTE r = GetRValue(colour);
  BYTE g = GetGValue(colour);
  BYTE b = GetBValue(colour);
  r = (BYTE)(r * 0.9333);
  g = (BYTE)(g * 0.9333);
  b = (BYTE)(b * 0.9333);
  return RGB(r,g,b);
}

void FindInFiles::SetResultsWidths(void)
{
  CRect resultsRect;
  m_resultsList.GetWindowRect(resultsRect);
  m_resultsList.SetColumnWidth(0,(int)(0.65 * resultsRect.Width()));
  m_resultsList.SetColumnWidth(1,(int)(0.2 * resultsRect.Width()));
  m_resultsList.SetColumnWidth(2,LVSCW_AUTOSIZE_USEHEADER);
}

FindInFiles::FindResult::FindResult()
{
  inContext.cpMin = 0;
  inContext.cpMax = 0;
  inSource.cpMin = 0;
  inSource.cpMax = 0;
  colour = InformApp::ColourBack;
}
