#include "projectmanager.h"
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStandardPaths>
#include <QCoreApplication>

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
    , m_buildProcess(new QProcess(this))
{
    connect(m_buildProcess, &QProcess::readyReadStandardOutput, this, [this](){
        QString output = m_buildProcess->readAllStandardOutput();
        emit buildProgress(output);
    });
    connect(m_buildProcess, &QProcess::readyReadStandardError, this, [this](){
        QString output = m_buildProcess->readAllStandardError();
        emit buildProgress(output);
    });
    connect(m_buildProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status){
        QString output = m_buildProcess->readAllStandardOutput() + m_buildProcess->readAllStandardError();
        bool success = (exitCode == 0 && status == QProcess::NormalExit);
        emit buildFinished(success, output);
    });
}

bool ProjectManager::createProject(const QString &path, const QString &type, const QString &name)
{
    QDir dir(path);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) return false;
    }
    
    // Create project structure
    dir.mkpath("src");
    dir.mkpath("build");
    dir.mkpath("dist");
    
    // Copy template files
    QString sdkRoot = getSdkRoot();
    QString templateDir = sdkRoot + "/templates/" + type;
    
    if (QDir(templateDir).exists()) {
        if (!copyTemplate(templateDir, dir.filePath("src"), name)) {
            return false;
        }
    }
    
    // Generate CMakeLists.txt
    QString cmakeContent = generateCMakeLists(path, type, name);
    QFile cmakeFile(dir.filePath("CMakeLists.txt"));
    if (!cmakeFile.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&cmakeFile);
    out << cmakeContent;
    cmakeFile.close();
    
    // Create .kresten project file
    QFile projectFile(dir.filePath(name + ".kresten"));
    if (projectFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&projectFile);
        out << "[Project]\n";
        out << "Name=" << name << "\n";
        out << "Type=" << type << "\n";
        out << "Version=1.0.0\n";
        out << "SDK=" << sdkRoot << "\n";
        projectFile.close();
    }
    
    return true;
}

bool ProjectManager::copyTemplate(const QString &srcDir, const QString &dstDir, const QString &projectName)
{
    QDir src(srcDir);
    QDir dst(dstDir);
    if (!dst.exists()) dst.mkpath(".");
    
    foreach (QFileInfo info, src.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
        QString srcFile = info.absoluteFilePath();
        QString dstFile = dst.filePath(info.fileName());
        
        QFile file(srcFile);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray content = file.readAll();
        file.close();
        
        // Replace template variables
        content.replace("my_plugin", projectName);
        content.replace("my_driver", projectName);
        content.replace("MY_PLUGIN", projectName.toUpper());
        content.replace("MY_DRIVER", projectName.toUpper());
        
        QFile outFile(dstFile);
        if (!outFile.open(QIODevice::WriteOnly)) continue;
        outFile.write(content);
        outFile.close();
    }
    
    return true;
}

QString ProjectManager::generateCMakeLists(const QString &projectPath, const QString &type, const QString &name)
{
    QString sdkRoot = getSdkRoot();
    QString toolchain = sdkRoot + "/toolchain/kresten-x86_64.cmake";
    QString suffix = (type == "plugin") ? ".kplg" : ".kdrv";
    QString linkLibs = (type == "plugin") ? "-lc -ldriver" : "-lc";
    
    QString cmake = QString(R"(
cmake_minimum_required(VERSION 3.10)
project(%1 LANGUAGES C)

# Use Kresten toolchain
set(CMAKE_TOOLCHAIN_FILE "%2")

# Output naming
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX "%3")

# Sources
set(SOURCES
    src/%1.c
)

add_library(%1 SHARED ${SOURCES})

target_compile_options(%1 PRIVATE
    -fPIC
    -fvisibility=hidden
    -Wall -Werror
    -m64 -mno-red-zone
)

target_link_libraries(%1 PRIVATE %4)

set_target_properties(%1 PROPERTIES
    OUTPUT_NAME %1
    SUFFIX %3
    PREFIX ""
)
)").arg(name, toolchain, suffix, linkLibs);
    
    return cmake;
}

bool ProjectManager::loadProject(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) return false;
    
    // Find .kresten project file
    QStringList projectFiles = dir.entryList(QStringList() << "*.kresten", QDir::Files);
    if (projectFiles.isEmpty()) {
        // Try to detect from CMakeLists.txt
        QFile cmake(path + "/CMakeLists.txt");
        if (!cmake.exists()) return false;
    }
    
    m_currentProjectPath = path;
    emit projectLoaded(path);
    return true;
}

void ProjectManager::buildProject(const QString &projectPath)
{
    m_currentProjectPath = projectPath;
    QString buildDir = projectPath + "/build";
    QDir().mkpath(buildDir);
    
    m_buildProcess->setWorkingDirectory(buildDir);
    
    // Configure with CMake
    QString sdkRoot = getSdkRoot();
    QString toolchain = sdkRoot + "/toolchain/kresten-x86_64.cmake";
    
    QStringList cmakeArgs;
    cmakeArgs << "-DCMAKE_TOOLCHAIN_FILE=" + toolchain
              << "-DCMAKE_BUILD_TYPE=Release"
              << "..";
    
    m_buildProcess->start("cmake", cmakeArgs);
    if (!m_buildProcess->waitForStarted(5000)) {
        emit buildFinished(false, "Failed to start cmake");
        return;
    }
    
    if (!m_buildProcess->waitForFinished(30000)) {
        m_buildProcess->kill();
        emit buildFinished(false, "CMake configure timeout");
        return;
    }
    
    // Build with cmake --build
    QStringList buildArgs = {"--build", ".", "--config", "Release"};
    m_buildProcess->start("cmake", buildArgs);
}

void ProjectManager::cleanProject(const QString &projectPath)
{
    QString buildDir = projectPath + "/build";
    QDir dir(buildDir);
    if (dir.exists()) {
        dir.removeRecursively();
    }
    emit buildFinished(true, "Clean completed");
}

void ProjectManager::runProject(const QString &projectPath)
{
    QString distDir = projectPath + "/dist";
    QDir dir(distDir);
    QStringList files = dir.entryList(QStringList() << "*.kplg" << "*.kdrv", QDir::Files);
    
    if (files.isEmpty()) {
        emit buildProgress("No .kplg or .kdrv files found in dist/");
        return;
    }
    
    // Run with QEMU using the SDK's build_iso.ps1 or direct QEMU
    QString sdkRoot = getSdkRoot();
    QString qemuCmd = "qemu-system-x86_64";
    
    // For now, just show the built files
    emit buildProgress("Built files in " + distDir + ":");
    for (const QString &f : files) {
        emit buildProgress("  " + f + " (" + QString::number(QFileInfo(distDir + "/" + f).size()) + " bytes)");
    }
}

QString ProjectManager::detectProjectType(const QString &path) const
{
    QDir dir(path);
    QStringList projectFiles = dir.entryList(QStringList() << "*.kresten", QDir::Files);
    if (!projectFiles.isEmpty()) {
        QFile file(dir.filePath(projectFiles.first()));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            QString content = in.readAll();
            if (content.contains("Type=plugin")) return "plugin";
            if (content.contains("Type=driver")) return "driver";
        }
    }
    return "plugin";
}

QString ProjectManager::getBuildCommand(const QString &projectPath, const QString &target) const
{
    QString sdkRoot = getSdkRoot();
    return sdkRoot + "/bin/x86_64-kresten-gcc.bat";
}

QString ProjectManager::getSdkRoot() const
{
    // Try to find SDK root relative to executable
    QString appDir = QCoreApplication::applicationDirPath();
    QString sdkPath = appDir + "/../";
    QDir dir(sdkPath);
    if (dir.exists("sysroot") && dir.exists("bin")) {
        return QFileInfo(sdkPath).absoluteFilePath();
    }
    // Fallback to default Windows SDK location
    return "C:/Users/Lenovo/Documents/New OpenCode Project/Kresten/SDK-Windows";
}