#include "dialog/dlgnetmixconnect.h"

#include <QHostAddress>
#include <QIcon>

#include "moc_dlgnetmixconnect.cpp"
#include "netmix/netmixsessionmanager.h"
#include "util/logger.h"

namespace {
const mixxx::Logger kLogger("DlgNetmixConnect");

int statusToInt(NetmixSessionManager::SessionState state) {
    return static_cast<int>(state);
}

} // namespace

DlgNetmixConnect::DlgNetmixConnect(NetmixSessionManager* pSessionMgr,
        QWidget* parent)
        : QDialog(parent),
          Ui::DlgNetmixConnectDlg(),
          m_pSessionMgr(pSessionMgr) {
    setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/images/mixxx_icon.svg")));
    setWindowTitle(tr("Netmix Connect"));

    // Port range validation
    spinPort->setRange(1024, 65535);
    spinPort->setValue(21200);

    // Populate deck pre-assignment combos
    populateDeckCombos();

    // Wire buttons
    connect(btnConnect, &QPushButton::clicked,
            this, &DlgNetmixConnect::onConnectClicked);
    connect(btnDisconnect, &QPushButton::clicked,
            this, &DlgNetmixConnect::onDisconnectClicked);

    // Wire session manager state changes
    connect(m_pSessionMgr, &NetmixSessionManager::sessionStateChanged,
            this, &DlgNetmixConnect::onSessionStateChanged);

    // Wire RTT updates
    connect(m_pSessionMgr, &NetmixSessionManager::rttUpdated,
            this, &DlgNetmixConnect::onRttUpdated);

    // Initialize state
    onSessionStateChanged(statusToInt(m_pSessionMgr->state()));
}

void DlgNetmixConnect::populateDeckCombos() {
    // 4 deck combos for channels 1-4 (channel 0 is crossfader, not in UI)
    QComboBox* combos[4] = {
            comboDeck1, comboDeck2, comboDeck3, comboDeck4};

    for (int i = 0; i < 4; ++i) {
        combos[i]->clear();
        combos[i]->addItem(tr("Open"), 0);
        combos[i]->addItem(tr("Local"), 1);
        combos[i]->addItem(tr("Remote"), 2);
        combos[i]->setCurrentIndex(0);
    }
}

void DlgNetmixConnect::onConnectClicked() {
    if (m_pSessionMgr->state() != NetmixSessionManager::Idle) {
        return;
    }

    QString displayName = editDisplayName->text().trimmed();
    if (displayName.isEmpty()) {
        displayName = QStringLiteral("Mixxx");
    }

    // Push display name to manager
    m_pSessionMgr->setDisplayName(displayName);

    // Read pre-assignment combos (channel 1-4)
    QComboBox* combos[4] = {
            comboDeck1, comboDeck2, comboDeck3, comboDeck4};
    for (int i = 0; i < 4; ++i) {
        int assign = combos[i]->currentData().toInt();
        m_pSessionMgr->setPreassignment(i + 1, assign);
    }

    quint16 port = static_cast<quint16>(spinPort->value());

    if (radioHost->isChecked()) {
        kLogger.info() << "Hosting session on port" << port;
        m_pSessionMgr->hostSession(port);
    } else if (radioJoin->isChecked()) {
        QString ipText = editPeerIp->text().trimmed();
        QHostAddress address(ipText);
        if (address.isNull()) {
            kLogger.warning() << "Invalid peer IP:" << ipText;
            labelStatus->setText(tr("Invalid peer IP address"));
            return;
        }
        kLogger.info() << "Joining session at" << ipText << "port" << port;
        m_pSessionMgr->joinSession(address, port);
    }
}

void DlgNetmixConnect::onDisconnectClicked() {
    kLogger.info() << "Leaving session";
    m_pSessionMgr->leaveSession();
}

void DlgNetmixConnect::onSessionStateChanged(int state) {
    auto sessionState = static_cast<NetmixSessionManager::SessionState>(state);
    updateStatusLabel(state);
    updateButtonStates();

    bool idle = (sessionState == NetmixSessionManager::Idle);
    editDisplayName->setEnabled(idle);
    editPeerIp->setEnabled(idle);
    spinPort->setEnabled(idle);
    radioHost->setEnabled(idle);
    radioJoin->setEnabled(idle);
    comboDeck1->setEnabled(idle);
    comboDeck2->setEnabled(idle);
    comboDeck3->setEnabled(idle);
    comboDeck4->setEnabled(idle);
}

void DlgNetmixConnect::onRttUpdated(double rttMs) {
    labelRtt->setText(QStringLiteral("RTT: %1 ms").arg(rttMs, 0, 'f', 1));
}

void DlgNetmixConnect::updateButtonStates() {
    auto state = m_pSessionMgr->state();
    btnConnect->setEnabled(state == NetmixSessionManager::Idle);
    btnDisconnect->setEnabled(
            state == NetmixSessionManager::Connected ||
            state == NetmixSessionManager::Degraded);
}

void DlgNetmixConnect::updateStatusLabel(int state) {
    switch (static_cast<NetmixSessionManager::SessionState>(state)) {
    case NetmixSessionManager::Idle:
        labelStatus->setText(tr("Idle"));
        labelRtt->setText(tr("RTT: -- ms"));
        break;
    case NetmixSessionManager::Connecting:
        labelStatus->setText(tr("Connecting..."));
        break;
    case NetmixSessionManager::Connected:
        labelStatus->setText(tr("Connected"));
        break;
    case NetmixSessionManager::Degraded:
        labelStatus->setText(tr("Degraded"));
        break;
    }
}
