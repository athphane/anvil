#pragma once
#include <QObject>
class AnvilTests : public QObject {
    Q_OBJECT
private slots:
    void demoSearchAndFilters();
    void apiDiscoversAndPaginates();
    void apiRejectsForeignPagination();
    void apiRejectsMalformedResponses();
    void apiReportsRateLimits();
    void sshKeepsRemoteDataQuoted();
    void connectionLaunchesOmarchyTerminal();
    void themeReloadsAfterAtomicReplacement();
    void translatorFollowsLiveLanguageAndFallbacks();
    void realQmlInteractionAndScreenshots();
private:
    static void write(const QString &path, const QByteArray &contents);
};
