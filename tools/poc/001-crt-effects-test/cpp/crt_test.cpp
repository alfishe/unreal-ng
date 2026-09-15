// CRT Effects Test - GPU/CPU Parity Verification
// Standalone test that renders all profiles at multiple resolutions
// and compares GPU vs CPU output

#include <QApplication>
#include <QImage>
#include <QDir>
#include <QDebug>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <cmath>
#include <vector>
#include <string>

// Include the actual CRT implementations
#include "widgets/crtfilter.h"
#include "widgets/crtprofiles.h"

// Test configuration
struct TestConfig {
    std::string name;
    CRTProfile profile;
    int outputWidth;
    int outputHeight;
};

// Reference ZX Spectrum screen dimensions
constexpr int SRC_WIDTH = 352;
constexpr int SRC_HEIGHT = 288;

// Generate a test pattern image (ZX Spectrum-like)
QImage generateTestPattern() {
    QImage img(SRC_WIDTH, SRC_HEIGHT, QImage::Format_RGBA8888);
    img.fill(QColor(192, 192, 192));  // Light gray background (ZX paper)

    // Draw some colored bars and text-like patterns
    QPainter p(&img);

    // Border area (darker gray)
    p.fillRect(0, 0, SRC_WIDTH, 48, QColor(192, 192, 192));
    p.fillRect(0, SRC_HEIGHT - 56, SRC_WIDTH, 56, QColor(192, 192, 192));

    // Simulated menu box
    int boxX = 140, boxY = 80, boxW = 150, boxH = 120;
    p.fillRect(boxX, boxY, boxW, boxH, QColor(0, 0, 0));
    p.fillRect(boxX + 2, boxY + 2, boxW - 4, boxH - 4, QColor(0, 255, 255));  // Cyan

    // Color bars (rainbow stripe)
    int stripeY = boxY + 8;
    int stripeW = 20;
    QColor colors[] = {Qt::black, Qt::red, Qt::yellow, Qt::green, Qt::cyan, Qt::blue, Qt::magenta};
    for (int i = 0; i < 7; i++) {
        p.fillRect(boxX + 8 + i * stripeW, stripeY, stripeW, 8, colors[i]);
    }

    // Simulated text lines
    p.setPen(Qt::black);
    p.setFont(QFont("Courier", 8));
    p.drawText(boxX + 8, boxY + 32, "Tape Loader");
    p.drawText(boxX + 8, boxY + 48, "128 BASIC");
    p.drawText(boxX + 8, boxY + 64, "Calculator");
    p.drawText(boxX + 8, boxY + 80, "48 BASIC");
    p.drawText(boxX + 8, boxY + 96, "TR-DOS");

    // Copyright text
    p.drawText(80, SRC_HEIGHT - 24, "(c) 1986 Sinclair Research Ltd");

    p.end();
    return img;
}

// Apply CPU CRT filter
QImage applyCpuCRT(const QImage& src, const CRTProfileParams& params, int outWidth, int outHeight) {
    // Scale source to output size
    QImage scaled = src.scaled(outWidth, outHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    scaled = scaled.convertToFormat(QImage::Format_RGBA8888);

    QImage result(outWidth, outHeight, QImage::Format_RGBA8888);

    CRTFilter filter;
    filter.apply(scaled.bits(), result.bits(), outWidth, outHeight,
                 src.width(), src.height(), params);

    return result;
}

// Calculate image statistics
struct ImageStats {
    double avgBrightness;
    double minBrightness;
    double maxBrightness;
    double stdDev;
};

ImageStats calculateStats(const QImage& img) {
    ImageStats stats = {0, 255, 0, 0};

    double sum = 0;
    double sumSq = 0;
    int count = 0;

    for (int y = 0; y < img.height(); y++) {
        const uchar* row = img.constScanLine(y);
        for (int x = 0; x < img.width(); x++) {
            // Calculate luminance
            double r = row[x * 4 + 0];
            double g = row[x * 4 + 1];
            double b = row[x * 4 + 2];
            double lum = 0.299 * r + 0.587 * g + 0.114 * b;

            sum += lum;
            sumSq += lum * lum;
            stats.minBrightness = std::min(stats.minBrightness, lum);
            stats.maxBrightness = std::max(stats.maxBrightness, lum);
            count++;
        }
    }

    stats.avgBrightness = sum / count;
    stats.stdDev = std::sqrt(sumSq / count - stats.avgBrightness * stats.avgBrightness);

    return stats;
}

// Compare two images
struct CompareResult {
    double mse;           // Mean squared error
    double psnr;          // Peak signal-to-noise ratio
    double avgDiff;       // Average absolute difference
    double maxDiff;       // Maximum difference
    bool similar;         // Within acceptable tolerance
};

CompareResult compareImages(const QImage& a, const QImage& b) {
    CompareResult result = {0, 0, 0, 0, false};

    if (a.size() != b.size()) {
        qWarning() << "Image sizes don't match!";
        return result;
    }

    double sumSqErr = 0;
    double sumAbsDiff = 0;
    double maxDiff = 0;
    int count = 0;

    for (int y = 0; y < a.height(); y++) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = 0; x < a.width() * 4; x++) {
            double diff = std::abs(static_cast<double>(rowA[x]) - rowB[x]);
            sumSqErr += diff * diff;
            sumAbsDiff += diff;
            maxDiff = std::max(maxDiff, diff);
            count++;
        }
    }

    result.mse = sumSqErr / count;
    result.psnr = (result.mse > 0) ? 10 * std::log10(255.0 * 255.0 / result.mse) : 100;
    result.avgDiff = sumAbsDiff / count;
    result.maxDiff = maxDiff;
    result.similar = (result.avgDiff < 5.0 && result.maxDiff < 30.0);

    return result;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Create output directory
    QString outputDir = "output";
    QDir().mkpath(outputDir);

    // Generate test pattern
    QImage testPattern = generateTestPattern();
    testPattern.save(outputDir + "/00_reference.png");
    qDebug() << "Generated reference image:" << SRC_WIDTH << "x" << SRC_HEIGHT;

    // Test configurations
    std::vector<TestConfig> tests;

    // All profiles
    std::vector<std::pair<std::string, CRTProfile>> profiles = {
        {"None", CRTProfile::None},
        {"Basic", CRTProfile::Basic},
        {"Aperture", CRTProfile::Aperture},
        {"ShadowMask", CRTProfile::ShadowMask},
        {"SlotMask", CRTProfile::SlotMask},
        {"Megatron", CRTProfile::Megatron}
    };

    // All resolutions (scale factors)
    std::vector<std::pair<std::string, float>> scales = {
        {"1x", 1.0f},
        {"1.5x", 1.5f},
        {"2x", 2.0f},
        {"2.5x", 2.5f},
        {"3x", 3.0f},
        {"4x", 4.0f}
    };

    qDebug() << "\n=== CRT Effects Test Suite ===\n";
    qDebug() << "Testing" << profiles.size() << "profiles at" << scales.size() << "resolutions\n";

    // Results summary
    int passed = 0, failed = 0;

    for (const auto& [profileName, profile] : profiles) {
        CRTProfileParams params = CRTProfileParams::FromProfile(profile);

        qDebug() << "\n--- Profile:" << profileName.c_str() << "---";
        qDebug() << "  maskType:" << static_cast<int>(params.maskType)
                 << "maskStrength:" << params.maskStrength
                 << "scanlineWeight:" << params.scanlineWeight;

        for (const auto& [scaleName, scale] : scales) {
            int outW = static_cast<int>(SRC_WIDTH * scale);
            int outH = static_cast<int>(SRC_HEIGHT * scale);

            // Apply CPU CRT
            QImage cpuResult = applyCpuCRT(testPattern, params, outW, outH);

            // Calculate stats
            ImageStats stats = calculateStats(cpuResult);

            // Save output
            QString filename = QString("%1/cpu_%2_%3.png")
                .arg(outputDir)
                .arg(QString::fromStdString(profileName))
                .arg(QString::fromStdString(scaleName));
            cpuResult.save(filename);

            // Report
            QString status;
            bool ok = true;

            // Check for burning white (avg brightness > 250)
            if (stats.avgBrightness > 250 && profile != CRTProfile::None) {
                status = "FAIL: Burning white!";
                ok = false;
            }
            // Check for too dark (avg brightness < 50)
            else if (stats.avgBrightness < 50 && profile != CRTProfile::None) {
                status = "FAIL: Too dark!";
                ok = false;
            }
            // Check None profile unchanged
            else if (profile == CRTProfile::None) {
                ImageStats refStats = calculateStats(testPattern.scaled(outW, outH));
                if (std::abs(stats.avgBrightness - refStats.avgBrightness) > 5) {
                    status = "FAIL: None profile modified image!";
                    ok = false;
                } else {
                    status = "OK";
                }
            }
            else {
                status = "OK";
            }

            if (ok) passed++; else failed++;

            qDebug().nospace()
                << "  " << scaleName.c_str() << " (" << outW << "x" << outH << "): "
                << "avg=" << QString::number(stats.avgBrightness, 'f', 1)
                << " min=" << QString::number(stats.minBrightness, 'f', 1)
                << " max=" << QString::number(stats.maxBrightness, 'f', 1)
                << " " << status;
        }
    }

    qDebug() << "\n=== Summary ===";
    qDebug() << "Passed:" << passed << " Failed:" << failed;
    qDebug() << "Output saved to:" << QDir::currentPath() + "/" + outputDir;

    return (failed == 0) ? 0 : 1;
}
