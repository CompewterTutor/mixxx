#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefnetmixdlg.h"
#include "preferences/usersettings.h"

class DlgPrefNetmix : public DlgPreferencePage, public Ui::DlgPrefNetmixDlg {
    Q_OBJECT
  public:
    DlgPrefNetmix(QWidget* parent, UserSettingsPointer pConfig);
    virtual ~DlgPrefNetmix();

  public slots:
    void slotApply() override;
    void slotUpdate() override;
    void slotResetToDefaults() override;

  private slots:
    void slotClearCache();

  private:
    void updateCacheInfo();

    UserSettingsPointer m_pConfig;

    int m_port;
    QString m_displayName;
    int m_rollbackWindow;
    bool m_quantizeEnabled;

    QString m_cacheDir;
};
