#include "RunRecorder.h"
#include "KpiData.h"
#include "Config.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QDateTime>
#include <QCoreApplication>
#include <cmath>
#include <utility>   // std::as_const
#include <QRegularExpression>
#include <QSet>

RunRecorder::RunRecorder(KpiData *kpi, Config *cfg, QObject *parent)
    : QObject(parent), m_kpi(kpi), m_cfg(cfg)
{
    // §4.2 KPI set (mirrors config.json acceptance targets used elsewhere).
    m_specs = {
        { "Path dev",   "mm", m_cfg->targetPathDeviationMm(), false },
        { "Detect lat", "ms", m_cfg->targetDetectLatencyMs(), false },
        { "Inference",  "ms", m_cfg->targetInferenceMs(),     false },
        { "Speedup",    "x",  m_cfg->targetSpeedupRatio(),    true  },
        { "CAN TX",     "ms", m_cfg->targetCanTxLatencyMs(),  false },
        { "Frame loss", "%",  m_cfg->canLossPctMax(),         false },
        { "Lane dev",   "mm", m_cfg->laneCenterDevMmMax(),    false },  // localization accuracy (EKF)
    };
    m_campaign = m_cfg->campaign();
    refreshRuns();
}

QString RunRecorder::runsDir() const {
    // Default: a "runs" folder next to the executable — convenient on the
    // desktop/bridge where you want the CSVs alongside the build. BUT that path
    // is read-only inside the iOS app bundle, and can be non-writable in some
    // WSL/Linux run locations, which made startRun() fail *silently* (open()
    // returns false → no run, no error). So if the app dir isn't writable, fall
    // back to the per-user app-data dir (always writable: AppData on Windows,
    // ~/.local/share on Linux, Library/Application Support on iOS).
    QDir appDir(QCoreApplication::applicationDirPath());
    if (appDir.mkpath("runs") && QFileInfo(appDir.filePath("runs")).isWritable())
        return appDir.filePath("runs");

    QDir dataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    dataDir.mkpath("runs");
    return dataDir.filePath("runs");
}

QString RunRecorder::runPath(const QString &name) const {
    return QDir(runsDir()).filePath(name);
}

double RunRecorder::kpiValue(const KpiSpec &s) const {
    if (s.name == "Path dev")   return m_kpi->pathDeviationMm();
    if (s.name == "Detect lat") return m_kpi->detectLatencyMs();
    if (s.name == "Inference")  return m_kpi->inferenceMs();
    if (s.name == "Speedup")    return m_cfg->ptBaselineMs() / std::max(1.0, m_kpi->inferenceMs());
    if (s.name == "CAN TX")     return m_kpi->canTxLatencyMs();
    if (s.name == "Frame loss") return m_kpi->frameLossPct();
    if (s.name == "Lane dev")   return m_kpi->laneCenterDeviationMm();
    return 0.0;
}

void RunRecorder::toggleRecording() {
    m_armed = !m_armed;
    if (!m_armed && m_inRun) finalizeRun();   // disarm mid-run → close it
    emit stateChanged();
}

// Arm auto-record when the operator sets a navigation goal. The actual run is
// captured on the subsequent AUTO drive (and never while replaying).
void RunRecorder::onGoalSet() {
    if (!m_armed) { m_armed = true; emit stateChanged(); }
}

// ── Frame stream (queued from worker) — persist raw bytes while in a run ──
void RunRecorder::onFrame(quint32 id, const QByteArray &payload, qint64 tsMs) {
    if (!m_inRun || !m_file.isOpen()) return;
    m_stream << tsMs << ',' << "0x" << QString::number(id, 16).toUpper()
             << ',' << payload.size() << ',' << QString::fromLatin1(payload.toHex()) << '\n';
}

// ── KPI cadence — drives run boundaries (Driving_State) + sampling ───────
//
// A run = ONE autonomous drive: it starts when the vehicle enters AUTO (i.e.
// a goal was set and autonomous driving began) and ends when it leaves AUTO
// (arrived → IDLE, or STOP/ERROR). Recording is NEVER active while replaying a
// past run — otherwise playing back a CSV would record a duplicate run (bug).
void RunRecorder::onKpiTick() {
    const QString cur = m_kpi->driveState();
    const bool replaying = m_kpi->replaying();

    // One-shot diagnostic on entering AUTO — makes it obvious in debug-log.txt
    // why a run did/didn't start (was it armed? replaying? already running?).
    if (cur == "AUTO" && m_prevState != "AUTO")
        qInfo().noquote() << "[REC] AUTO entered — armed=" << m_armed
                          << "replaying=" << replaying << "inRun=" << m_inRun;

    if (m_armed && !replaying && !m_inRun && cur == "AUTO")
        startRun();
    else if (m_inRun && (replaying || cur != "AUTO"))
        finalizeRun();

    if (m_inRun && !replaying) {
        for (const auto &s : std::as_const(m_specs)) {
            const double v = kpiValue(s);
            Acc &a = m_runAcc[s.name];
            a.sum += v; a.sumSq += v * v; a.n += 1;
        }
        ++m_sampleCount;
        emit stateChanged();
    }
    m_prevState = cur;
}

void RunRecorder::startRun() {
    // Tag the run with the active campaign (in filename + a header line) so the
    // aggregate can scope to one campaign and ignore early dev runs.
    QString safeTag = m_campaign;
    safeTag.replace(QRegularExpression("[^A-Za-z0-9-]"), "");   // filename-safe
    if (safeTag.isEmpty()) safeTag = "dev";
    const QString name = "run_" + safeTag + "_"
                       + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".csv";
    m_file.setFileName(runPath(name));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        // The primary runs dir reported writable but the actual file open was
        // rejected (AV quarantine, OneDrive sync lock, or a perms quirk). Retry
        // in AppData — the per-user, always-writable location — so a real AUTO
        // drive is never lost to a silent open failure.
        QDir dataDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        dataDir.mkpath("runs");
        m_file.setFileName(dataDir.filePath("runs/" + name));
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning().noquote() << "[REC] cannot open run file:" << m_file.fileName();
            return;
        }
    }
    qInfo().noquote() << "[REC] recording →" << m_file.fileName();
    m_stream.setDevice(&m_file);
    m_stream << "# campaign," << m_campaign << "\n";
    // Destination this run was driving to (the goal you set/clicked), so the run
    // log records WHERE it was headed. Omitted when no goal is active.
    if (m_kpi->goalActive())
        m_stream << "# goal," << m_kpi->goalDistM() << ',' << m_kpi->goalLatM()
                 << ',' << m_kpi->goalYawDeg() << "\n";
    m_stream << "# ts_ms,id,len,bytes_hex\n";
    m_runAcc.clear();
    m_sampleCount = 0;
    m_inRun = true;
    emit stateChanged();
}

void RunRecorder::finalizeRun() {
    if (m_file.isOpen()) {
        // Append per-run KPI means as trailing comments (also re-read on refresh).
        for (const auto &s : std::as_const(m_specs)) {
            const Acc &a = m_runAcc.value(s.name);
            if (a.n == 0) continue;     // skip empty runs so they don't skew the aggregate
            m_stream << "# KPI," << s.name << ',' << (a.sum / a.n) << ',' << s.unit << '\n';
        }
        m_stream.flush();
        m_file.close();
    }
    m_inRun = false;
    // One-shot per goal: a completed autonomous drive ends auto-record. The
    // next goal re-arms it (onGoalSet).
    m_armed = false;
    refreshRuns();          // re-reads all runs (incl. this one) → rebuilds means
    emit stateChanged();
}

void RunRecorder::recomputeAcrossRun() {
    QVariantList results;
    int passes = 0;
    for (const auto &s : std::as_const(m_specs)) {
        const QVector<double> &means = m_runMeans.value(s.name);
        double mean = 0, var = 0;
        const int n = means.size();
        if (n > 0) {
            for (double m : means) mean += m;
            mean /= n;
            for (double m : means) var += (m - mean) * (m - mean);
            var = n > 1 ? var / (n - 1) : 0.0;
        }
        const double sd = std::sqrt(var);
        const bool pass = n > 0 && (s.higherIsBetter ? mean >= s.target : mean <= s.target);
        if (pass) ++passes;
        QVariantMap row;
        row["name"]   = s.name;
        row["unit"]   = s.unit;
        row["mean"]   = mean;
        row["std"]    = sd;
        row["target"] = s.target;
        row["higher"] = s.higherIsBetter;
        row["pass"]   = pass;
        row["runs"]   = n;
        results.push_back(row);
    }
    m_kpiResults = results;
    m_lastSummary = QString("campaign '%1' · %2 runs in scope · %3/%4 KPIs passing (N target %5)")
                        .arg(m_campaign)
                        .arg(m_runsInScope)
                        .arg(passes).arg(m_specs.size())
                        .arg(m_cfg->runsTotal());
    emit runsChanged();
}

// Client mirror: replace the run list with the names the bridge advertises.
// Marks this recorder "external" so subsequent refreshRuns() won't re-scan the
// (empty) local disk and wipe it. runPath() still works — it just joins the
// local runs dir, and StateLink sends only the basename, which the bridge
// re-resolves against its own dir.
void RunRecorder::setExternalRunList(const QStringList &runs) {
    m_external = true;
    if (runs == m_runFiles) return;
    m_runFiles = runs;
    emit runsChanged();
}

void RunRecorder::refreshRuns() {
    if (m_external) return;   // client: list is mirrored from the bridge, not disk
    QDir d(runsDir());
    m_runFiles = d.entryList(QStringList() << "run_*.csv", QDir::Files, QDir::Name | QDir::Reversed);

    // Rebuild per-KPI run means from the run files' "# KPI,name,mean,unit" lines,
    // but ONLY for files whose "# campaign" matches the active campaign (A6).
    // This keeps run count + aggregate consistent and survives restart.
    m_runMeans.clear();
    m_runsInScope = 0;
    QSet<QString> tags;
    const int nCap = m_cfg->runsTotal();
    for (const QString &fn : std::as_const(m_runFiles)) {
        QFile f(runPath(fn));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        QString tag = "dev";
        QVector<QPair<QString,double>> means;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.startsWith("# campaign,")) {
                tag = line.mid(QStringLiteral("# campaign,").size()).trimmed();
            } else if (line.startsWith("# KPI,")) {
                const QStringList c = line.mid(6).split(',');
                if (c.size() < 2) continue;
                bool ok = false; const double mean = c[1].trimmed().toDouble(&ok);
                if (ok) means.push_back({ c[0].trimmed(), mean });
            }
        }
        f.close();
        tags.insert(tag);
        if (tag != m_campaign) continue;                 // out of scope
        if (nCap > 0 && m_runsInScope >= nCap) continue;  // cap to most recent N in scope
        ++m_runsInScope;
        for (const auto &m : means) m_runMeans[m.first].push_back(m.second);
    }
    m_campaigns = QStringList(tags.values());
    m_campaigns.sort();
    recomputeAcrossRun();   // emits runsChanged()
}

// Export the across-run aggregate table to runs/summary.csv.
void RunRecorder::exportSummary() {
    recomputeAcrossRun();
    QFile f(QDir(runsDir()).filePath("summary.csv"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream out(&f);
    out << "kpi,unit,mean,std,target,direction,pass,runs\n";
    for (const QVariant &v : std::as_const(m_kpiResults)) {
        const QVariantMap r = v.toMap();
        out << r["name"].toString() << ',' << r["unit"].toString() << ','
            << r["mean"].toDouble() << ',' << r["std"].toDouble() << ','
            << r["target"].toDouble() << ','
            << (r["higher"].toBool() ? "min" : "max") << ','
            << (r["pass"].toBool() ? "PASS" : "FAIL") << ',' << r["runs"].toInt() << '\n';
    }
    f.close();
    refreshRuns();
}
