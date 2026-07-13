#include "preferences/dialog/dlgprefnetmix.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "moc_dlgprefnetmix.cpp"

namespace {
constexpr int kDefaultPort = 21200;
constexpr int kDefaultRollbackWindow = 8;
constexpr bool kDefaultQuantizeEnabled = false;

const char* kConfigGroup = "[Netmix]";
const char* kConfigPort = "Port";
const char* kConfigDisplayName = "DisplayName";
const char* kConfigRollbackWindow = "RollbackWindow";
const char* kConfigQuantize = "QuantizeEnabled";

QString formatByteSize(qint64 bytes) {
    if (bytes < 1024) {
        return QString::number(bytes) + QStringLiteral(" B");
    }
    if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KiB");
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MiB");
    }
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + QStringLiteral(" GiB");
}
} // anonymous namespace

DlgPrefNetmix::DlgPrefNetmix(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(pConfig),
          m_port(kDefaultPort),
          m_rollbackWindow(kDefaultRollbackWindow),
          m_quantizeEnabled(kDefaultQuantizeEnabled) {
    setupUi(this);

    m_cacheDir = m_pConfig->getSettingsPath() + QStringLiteral("/netmix_cache/");

    slotUpdate();

    connect(clearCacheButton,
            &QPushButton::clicked,
            this,
            &DlgPrefNetmix::slotClearCache);

    setScrollSafeGuard(portSpinBox);
    setScrollSafeGuard(rollbackWindowSpinBox);
}

DlgPrefNetmix::~DlgPrefNetmix() = default;

void DlgPrefNetmix::slotUpdate() {
    m_port = m_pConfig->getValue<int>(
            ConfigKey(kConfigGroup, kConfigPort), kDefaultPort);
    m_displayName = m_pConfig->getValueString(
            ConfigKey(kConfigGroup, kConfigDisplayName));
    m_rollbackWindow = m_pConfig->getValue<int>(
            ConfigKey(kConfigGroup, kConfigRollbackWindow), kDefaultRollbackWindow);
    m_quantizeEnabled = m_pConfig->getValue<bool>(
            ConfigKey(kConfigGroup, kConfigQuantize), kDefaultQuantizeEnabled);

    portSpinBox->setValue(m_port);
    displayNameEdit->setText(m_displayName);
    rollbackWindowSpinBox->setValue(m_rollbackWindow);
    quantizeCheckBox->setChecked(m_quantizeEnabled);

    updateCacheInfo();
}

void DlgPrefNetmix::slotApply() {
    m_port = portSpinBox->value();
    m_displayName = displayNameEdit->text();
    m_rollbackWindow = rollbackWindowSpinBox->value();
    m_quantizeEnabled = quantizeCheckBox->isChecked();

    m_pConfig->setValue(ConfigKey(kConfigGroup, kConfigPort), m_port);
    m_pConfig->setValue(ConfigKey(kConfigGroup, kConfigDisplayName), m_displayName);
    m_pConfig->setValue(ConfigKey(kConfigGroup, kConfigRollbackWindow), m_rollbackWindow);
    m_pConfig->setValue(ConfigKey(kConfigGroup, kConfigQuantize), m_quantizeEnabled);
}

void DlgPrefNetmix::slotResetToDefaults() {
    portSpinBox->setValue(kDefaultPort);
    displayNameEdit->setText(QString());
    rollbackWindowSpinBox->setValue(kDefaultRollbackWindow);
    quantizeCheckBox->setChecked(kDefaultQuantizeEnabled);

    m_port = kDefaultPort;
    m_displayName = QString();
    m_rollbackWindow = kDefaultRollbackWindow;
    m_quantizeEnabled = kDefaultQuantizeEnabled;

    updateCacheInfo();
}

void DlgPrefNetmix::slotClearCache() {
    QDir cacheDir(m_cacheDir);
    if (cacheDir.exists()) {
        cacheDir.removeRecursively();
        cacheDir.mkpath(QStringLiteral("."));
    }
    updateCacheInfo();
}

void DlgPrefNetmix::updateCacheInfo() {
    cachePathLabel->setText(m_cacheDir);

    QDir cacheDir(m_cacheDir);
    if (!cacheDir.exists()) {
        cacheSizeLabel->setText(tr("0 B (not yet created)"));
        return;
    }

    qint64 totalSize = 0;
    QDirIterator it(m_cacheDir,
            QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        totalSize += it.fileInfo().size();
    }

    cacheSizeLabel->setText(formatByteSize(totalSize));
}
