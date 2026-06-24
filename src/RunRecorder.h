#pragma once

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QVariantList>
#include <QHash>
#include <QVector>

class KpiData;
class Config;

// RunRecorder (Task 2) — records decoded RX frames to per-run CSV files and
// aggregates the §4.2 KPIs per-run and across-run.
//
// Lives on the MAIN (GUI) thread. It receives raw frames via the queued
// CanBridge::frameForRecord signal (worker→main) and samples KpiData on the
// existing kpiChanged cadence. Run boundaries follow Driving_State: a run
// opens when the vehicle enters AUTO and closes when it leaves AUTO
// (STOP/ERROR/IDLE) — reusing the live decode, no simulation here.
class RunRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool         recording  READ recording  NOTIFY stateChanged)
    Q_PROPERTY(bool         inRun      READ inRun      NOTIFY stateChanged)
    Q_PROPERTY(int          sampleCount READ sampleCount NOTIFY stateChanged)
    Q_PROPERTY(int          runCount   READ runCount   NOTIFY runsChanged)
    Q_PROPERTY(QStringList  runList    READ runList    NOTIFY runsChanged)
    Q_PROPERTY(QString      lastSummary READ lastSummary NOTIFY runsChanged)
    Q_PROPERTY(QVariantList kpiResults READ kpiResults NOTIFY runsChanged)
    // Campaign scoping (A6): aggregate only runs tagged with the active campaign.
    Q_PROPERTY(QString      campaign   READ campaign   WRITE setCampaign NOTIFY campaignChanged)
    Q_PROPERTY(QStringList  campaigns  READ campaigns  NOTIFY runsChanged)   // tags seen on disk
    Q_PROPERTY(int          runsInScope READ runsInScope NOTIFY runsChanged)

public:
    RunRecorder(KpiData *kpi, Config *cfg, QObject *parent = nullptr);

    bool         recording() const   { return m_armed; }
    bool         inRun() const       { return m_inRun; }
    int          sampleCount() const { return m_sampleCount; }
    int          runCount() const    { return m_runFiles.size(); }
    QStringList  runList() const     { return m_runFiles; }
    QString      lastSummary() const { return m_lastSummary; }
    QVariantList kpiResults() const  { return m_kpiResults; }
    QString      campaign() const    { return m_campaign; }
    QStringList  campaigns() const   { return m_campaigns; }
    int          runsInScope() const { return m_runsInScope; }
    void         setCampaign(const QString &c) {
        if (m_campaign == c) return;
        m_campaign = c; emit campaignChanged(); refreshRuns();
    }

    Q_INVOKABLE void    toggleRecording();
    Q_INVOKABLE void    refreshRuns();
    Q_INVOKABLE QString runPath(const QString &name) const;
    Q_INVOKABLE void    exportSummary();
    // Client (iPad) mirror: the run list is owned by the bridge and pushed over
    // UDP. Setting it here makes runList/runCount reflect the bridge's runs so
    // the shared Runs page works unchanged; disk scanning is then suppressed.
    void setExternalRunList(const QStringList &runs);

public slots:
    void onFrame(quint32 id, const QByteArray &payload, qint64 tsMs); // queued from CanBridge
    void onKpiTick();                                                 // kpiData::kpiChanged
    void onGoalSet();                                                 // kpiData::goalChanged → arm

signals:
    void stateChanged();
    void runsChanged();
    void campaignChanged();

private:
    struct Acc { double sum = 0, sumSq = 0; int n = 0; };
    struct KpiSpec { QString name, unit; double target; bool higherIsBetter; };

    void   startRun();
    void   finalizeRun();
    double kpiValue(const KpiSpec &s) const;     // read current KpiData value
    void   recomputeAcrossRun();
    QString runsDir() const;

    KpiData *m_kpi;
    Config  *m_cfg;

    bool    m_armed = false;    // auto-record arms when a goal is set (onGoalSet);
                               // then a run is captured on the AUTO drive. The
                               // UI toggle starts a MANUAL run immediately instead.
    bool    m_manual = false;   // true = manual REC toggle: capture the live feed
                               // regardless of Driving_State (hand-driven demos),
                               // and end only on toggle-off / replay, not AUTO exit.
    bool    m_external = false; // client: run list mirrored from the bridge (no
                               // local disk scan — see setExternalRunList).
    bool    m_inRun = false;
    QFile   m_file;
    QTextStream m_stream;
    QString m_prevState;
    int     m_sampleCount = 0;

    QVector<KpiSpec>          m_specs;       // §4.2 KPI set
    QHash<QString, Acc>       m_runAcc;      // current run accumulators (per KPI name)
    QHash<QString, QVector<double>> m_runMeans; // across-run: each run's mean per KPI

    QStringList  m_runFiles;
    QString      m_lastSummary;
    QVariantList m_kpiResults;
    QString      m_campaign = "dev";
    QStringList  m_campaigns;
    int          m_runsInScope = 0;
};
