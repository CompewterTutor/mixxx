#pragma once

#include <QDialog>

#include "dialog/ui_dlgnetmixconnectdlg.h"

class NetmixSessionManager;

class DlgNetmixConnect : public QDialog, public Ui::DlgNetmixConnectDlg {
    Q_OBJECT
  public:
    explicit DlgNetmixConnect(NetmixSessionManager* pSessionMgr,
            QWidget* parent = nullptr);
    ~DlgNetmixConnect() override = default;

  private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onSessionStateChanged(int state);
    void onRttUpdated(double rttMs);

  private:
    void populateDeckCombos();
    void updateButtonStates();
    void updateStatusLabel(int state);

    NetmixSessionManager* m_pSessionMgr;
};
