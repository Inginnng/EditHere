param([Parameter(Mandatory = $true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputPath = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $outputPath) { throw "Destination already exists: $outputPath" }
if ($outputPath.StartsWith($PSScriptRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose an export destination outside the feature source directory.'
}
New-Item -ItemType Directory -Path $outputPath, (Join-Path $outputPath 'features'), (Join-Path $outputPath 'app') | Out-Null
Copy-Item -LiteralPath $PSScriptRoot -Destination (Join-Path $outputPath 'features/ai-text') -Recurse
# Only shared widget/theme support and its local headers are needed by the host.
$queue = [Collections.Generic.Queue[string]]::new()
foreach ($name in @('ui.cpp', 'fonts.cpp', 'i18n.cpp', 'settings.h')) { $queue.Enqueue($name) }
$copied = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while ($queue.Count) {
    $name = $queue.Dequeue()
    if (-not $copied.Add($name)) { continue }
    $path = Join-Path $repoPath "app/$name"
    Copy-Item -LiteralPath $path -Destination (Join-Path $outputPath "app/$name")
    $content = Get-Content -Raw -LiteralPath $path
    foreach ($match in [regex]::Matches($content, '(?m)^#include "([^"/]+)"')) {
        if (Test-Path -LiteralPath (Join-Path $repoPath ('app/' + $match.Groups[1].Value))) { $queue.Enqueue($match.Groups[1].Value) }
    }
}
New-Item -ItemType Directory -Path (Join-Path $outputPath 'assets'), (Join-Path $outputPath 'cmake'), (Join-Path $outputPath 'packaging/licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoPath 'assets/ui-icons') -Destination (Join-Path $outputPath 'assets/ui-icons') -Recurse
New-Item -ItemType Directory -Path (Join-Path $outputPath 'assets/icons') | Out-Null
Copy-Item -LiteralPath (Join-Path $repoPath 'assets/icons/edithere.svg') -Destination (Join-Path $outputPath 'assets/icons')
Copy-Item -LiteralPath (Join-Path $repoPath 'cmake/WindowsTestRuntime.cmake') -Destination (Join-Path $outputPath 'cmake')
Copy-Item -LiteralPath (Join-Path $repoPath 'cmake/PatchQtKeychain.cmake') -Destination (Join-Path $outputPath 'cmake')
foreach ($name in @('qtkeychain', 'cc-switch', 'lobehub-icons')) {
    Copy-Item -LiteralPath (Join-Path $repoPath "packaging/licenses/$name") -Destination (Join-Path $outputPath "packaging/licenses/$name") -Recurse
}
foreach ($name in @('LICENSE', 'NOTICE', 'LICENSING.md', 'COMMERCIAL-LICENSE.md')) {
    Copy-Item -LiteralPath (Join-Path $repoPath $name) -Destination (Join-Path $outputPath $name)
}
@'
# EditHere AI 文字模块 · 独立源码包

功能定位：人用 EditHere 表达图形化需求，AI 把像素定位、拖动和缩放转成不看图也能理解的自然语言需求；目标对话只接收文字，结果不输出截图坐标或图像元数据。

这是可独立构建和编辑的源码副本。主要工作目录是 `features/ai-text/`，详细文件地图见其中的 README。`app/` 仅保留主题、字体、语言和类型声明；没有主编辑器、截图、OCR 和视频业务实现。

在此目录运行（根据机器替换 Qt 路径）：

```powershell
.\features\ai-text\dev.ps1 -Standalone -QtRoot D:\Qt\6.8.0\msvc2022_64
```

需要 Qt 6.8+、CMake、Ninja 和对应编译器。Windows MSVC 安装可自动发现；MinGW 请在相应工具链环境中运行。首次构建会获取固定版本的 QtKeychain。默认本地回放，用 `-Live` 才手动连接真实 API。

修改提示词、QSS 和示例后重开工作台；修改 C++ 后重跑脚本。可将这个文件夹直接交给其他 AI，要求它以 `features/ai-text/` 为修改范围。完成后，将该目录的修改回填主仓库同名目录。

独立构建可运行 `ai_feedback_tests`；窗口回归和工程快照测试在完整仓库运行。源码沿用原项目许可，根目录与 `packaging/licenses/` 保留了相关许可文件。
'@ | Set-Content -LiteralPath (Join-Path $outputPath 'README.md') -Encoding utf8
Write-Host "Exported: $outputPath"
