#pragma once

#include <set>

class ProjectSettings
{
public:
  ProjectSettings();

  void Load(const char* path);
  bool Save(const char* path);
  CTime GetFileTimestamp(const char* path);

  CString GetInformSwitches(bool release, bool debugFile);
  CString GetOutputFormat(void);
  CString GetOutputNewFormat(bool release);
  CString GetCompilerVersion(void);

  enum Output
  {
    OutputZ8 = 8,
    OutputGlulx = 256
  };

  Output m_output;
  bool m_blorb;
  bool m_predictable;
  bool m_basic;
  CString m_compilerVersion;
  int m_testingTabShownCount;

  bool m_changed;

private:
  void SetDefaults(void);
};
