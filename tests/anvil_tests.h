#pragma once
#include <QObject>
class AnvilTests : public QObject {
    Q_OBJECT
private slots:
    void demoSearchAndFilters();
    void apiDiscoversAndPaginates();
    void apiIncludesServersOnEverySitePage();
    void apiReportsUnmatchedSites();
    void apiReportsRequiredScopes();
    void apiRejectsForeignPagination();
    void apiRejectsMalformedResponses();
    void apiReportsRateLimits();
    void cacheReusesFreshInventoryAndExcludesSecrets();
    void cacheExpiresAfterSevenDays();
    void cacheSurvivesFailedRefresh();
    void cacheIsolatesTokensAndRecoversFromCorruption();
    void cacheForgetRemovesInventory();
    void cacheWriteFailureKeepsConnections();
    void sshKeepsRemoteDataQuoted();
    void connectionLaunchesOmarchyTerminal();
    void sshWorkingAccessSkipsForge();
    void sshInstallsPublicKeyForSiteUser();
    void sshExistingKeyIsNotUploadedAgain();
    void sshStopsOnHostOrNetworkFailures();
    void sshErrorsExplainTheNextStep();
    void sshReportsCreatePermission();
    void sshWaitsWithoutRepeatedUploads();
    void sshRejectsPrivateFilesAndForeignLinks();
    void sshCancellationStopsPolling();
    void sshPreparationCanRunWithoutTerminal();
    void themeReloadsAfterAtomicReplacement();
    void translatorFollowsLiveLanguageAndFallbacks();
    void realQmlInteractionAndScreenshots();
private:
    static void write(const QString &path, const QByteArray &contents);
};
