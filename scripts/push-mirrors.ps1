# 将本地分支与标签同步到 Gitee / AtomGit 镜像仓库
# 用法：
#   powershell -File scripts/push-mirrors.ps1                  # 推送当前分支 + 标签到 Gitee
#   powershell -File scripts/push-mirrors.ps1 -AllBranches      # 推送全部分支 + 标签
#   powershell -File scripts/push-mirrors.ps1 -DryRun           # 只演练，不真正写入
#   powershell -File scripts/push-mirrors.ps1 -ForceTags        # 覆盖远端指向不同的标签
#   powershell -File scripts/push-mirrors.ps1 -Targets gitee,atomgit  # 指定远程；AtomGit 只读会失败
#
# AtomGit 上的仓库是从 GitHub 拉取的只读镜像仓库，不接受推送（服务端返回
# "the repository is an image repository"）。它的内容由 AtomGit 自己跟随 GitHub，
# 不需要也不能由本地推送，因此默认只推 Gitee。
param(
    [string[]]$Targets = @('gitee'),
    [string]$Branch,
    [switch]$AllBranches,
    [switch]$SkipTags,
    [switch]$ForceTags,
    [switch]$DryRun
)

$root = & git rev-parse --show-toplevel 2>$null
if ($LASTEXITCODE -ne 0 -or -not $root) {
    Write-Host '请在 EditHere 仓库内运行本脚本。' -ForegroundColor Red
    exit 1
}
Set-Location $root

if (-not $Branch) {
    $Branch = (& git rev-parse --abbrev-ref HEAD).Trim()
}

$pushArgs = @()
if ($DryRun) { $pushArgs += '--dry-run' }

# 返回 0 表示远程存在
function Test-Remote([string]$name) {
    & git remote get-url $name *> $null
    return $LASTEXITCODE -eq 0
}

# 标签逐个比对：缺失的批量推送，指向冲突的默认跳过
function Push-MirrorTags([string]$target) {
    $remoteSha = @{}
    foreach ($line in (& git ls-remote --tags $target 2>$null)) {
        if (-not $line) { continue }
        $parts = $line -split "`t"
        if ($parts.Count -lt 2) { continue }
        $ref = $parts[1]
        if ($ref.EndsWith('^{}')) { continue }
        $remoteSha[($ref -replace '^refs/tags/', '')] = $parts[0]
    }

    $missing = @()
    $missingNames = @()
    $conflicts = @()
    foreach ($tag in (& git tag)) {
        if (-not $tag) { continue }
        $sha = (& git rev-parse "refs/tags/$tag").Trim()
        if (-not $remoteSha.ContainsKey($tag)) {
            $missing += "refs/tags/$tag"
            $missingNames += $tag
        } elseif ($remoteSha[$tag] -ne $sha) {
            $conflicts += $tag
        }
    }

    if ($missing.Count -gt 0) {
        Write-Host ("推送标签：" + ($missingNames -join ' '))
        & git push @pushArgs $target @missing
        if ($LASTEXITCODE -ne 0) { return $false }
    } else {
        Write-Host '标签已是最新。'
    }

    if ($conflicts.Count -gt 0) {
        if ($ForceTags) {
            Write-Host ("覆盖指向不同的标签：" + ($conflicts -join ' '))
            $refs = $conflicts | ForEach-Object { "refs/tags/$_" }
            & git push @pushArgs --force $target @refs
            if ($LASTEXITCODE -ne 0) { return $false }
        } else {
            Write-Host ("跳过已存在且指向不同的标签：" + ($conflicts -join ' ') + '（确认后可用 -ForceTags 覆盖）') -ForegroundColor Yellow
        }
    }
    return $true
}

$failed = @()
foreach ($target in $Targets) {
    if (-not (Test-Remote $target)) {
        Write-Host "跳过 $target：本地没有配置同名远程。" -ForegroundColor Yellow
        continue
    }
    Write-Host "==> 推送到 $target" -ForegroundColor Cyan

    if ($AllBranches) {
        & git push @pushArgs $target --all
    } else {
        & git push @pushArgs $target "refs/heads/${Branch}:refs/heads/${Branch}"
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "$target：分支推送失败。" -ForegroundColor Red
        $failed += $target
        continue
    }

    if (-not $SkipTags) {
        if (-not (Push-MirrorTags $target)) {
            Write-Host "$target：标签推送失败。" -ForegroundColor Red
            $failed += $target
            continue
        }
    }

    Write-Host "$target：完成。" -ForegroundColor Green
}

if ($failed.Count -gt 0) {
    Write-Host ("失败目标：" + ($failed -join '、')) -ForegroundColor Red
    Write-Host '认证问题见 docs/DEVELOPMENT.md 的「镜像推送」一节。'
    exit 1
}

Write-Host '全部镜像同步完成。' -ForegroundColor Green
