# 身体程序工作点扫描（主线的卡点：xlqm 那条链上"视觉信号的信噪比"到底能到什么程度）
#
# 用已经编好的 fly_mcns_view.exe 跑，不需要重编（另一个智能体正在改 cell_backend.h，
# 我这边不构建、避开冲突）。
#
# 判据全部用修正后的统计量（经验方差 + AR(1)）+ 假阳性期望 + 标签随机化对照：
#   · 信号组：真实逼近相位标签
#   · 对照组：--label-random（同参数、同时长）
#   · 要看的量：ol_intrinsic 的"显著数 / 对照显著数"（信噪比），以及对照是否落在假阳性期望附近
#
# 用法：powershell -File sweep_workpoint.ps1
$ErrorActionPreference = 'Continue'
$env:PATH = "C:\Qt\5.15.2\msvc2019_64\bin;D:\WorkSpace\xhqm_v2\binary;D:\WorkSpace\_research\digital-fruit-fly\depend\mujoco;D:\WorkSpace\xhqm_v2\depend\glew\bin;$env:PATH"
$exe  = "D:\WorkSpace\xhqm_v2\xhqm\test\ffbo\build\fly_mcns_view.exe"
$base = "D:\WorkSpace\_research\digital-fruit-fly\data\malecns"
$xml  = "D:\WorkSpace\_research\digital-fruit-fly\code\flygym\src\flygym\assets\model\flybody\fruitfly.xml"
$log  = "D:\WorkSpace\xhqm_v2\xhqm\test\ffbo\_sweep_workpoint.log"
"# 身体程序工作点扫描  开始 $(Get-Date -Format 'HH:mm:ss')" | Set-Content $log -Encoding UTF8
"# 列：sg=syn-gain  sen=sensor-gain  gb=graded-base  |  ol=ol_intrinsic 显著(对照)  全网显著(对照)  假阳性期望  ol群体d'" | Add-Content $log -Encoding UTF8

$SEC = '20'
$points = @(
    @{ sg = 16; sen = 1;  gb = 1.0 },
    @{ sg = 8;  sen = 1;  gb = 1.0 },
    @{ sg = 40; sen = 1;  gb = 1.0 },
    @{ sg = 16; sen = 5;  gb = 1.0 },
    @{ sg = 16; sen = 20; gb = 1.0 },
    @{ sg = 16; sen = 80; gb = 1.0 },
    @{ sg = 16; sen = 1;  gb = 0.5 },
    @{ sg = 16; sen = 20; gb = 0.5 },
    @{ sg = 16; sen = 20; gb = 2.0 },
    @{ sg = 60; sen = 20; gb = 1.0 }
)

foreach ($p in $points) {
    $row = [ordered]@{ sig_ol = -1; ctl_ol = -1; sig_all = -1; ctl_all = -1; fp = -1; ol_dp = '?' }
    foreach ($ctrl in @($false, $true)) {
        $a = @($base, $xml, '--sec', $SEC, '--predator', '--no-render',
               '--cell-model', 'bio', '--bio-graded',
               '--graded-base', "$($p.gb)", '--graded-amp', '1', '--norm-gain', '0',
               '--syn-gain', "$($p.sg)", '--sensor-gain', "$($p.sen)")
        if ($ctrl) { $a += '--label-random' }
        $o = & $exe @a 2>&1 | Out-String
        $mAll = [regex]::Match($o, '全网 (\d+) 个有发放细胞；\|Δ\|>3σ 的 (\d+) 个；假阳性期望 \d+×0\.27% ≈ (\d+)')
        $mOl  = [regex]::Match($o, 'ol_intrinsic\s+(\d+)\s+(\d+)\s+([\d.]+)')
        if ($mAll.Success) {
            if ($ctrl) { $row.ctl_all = [int]$mAll.Groups[2].Value; $row.fp = [int]$mAll.Groups[3].Value }
            else       { $row.sig_all = [int]$mAll.Groups[2].Value }
        }
        if ($mOl.Success) {
            if ($ctrl) { $row.ctl_ol = [int]$mOl.Groups[2].Value }
            else       { $row.sig_ol = [int]$mOl.Groups[2].Value; $row.ol_dp = $mOl.Groups[3].Value }
        }
    }
    $ratio = if ($row.ctl_ol -gt 0) { [math]::Round($row.sig_ol / $row.ctl_ol, 1) } else { 'inf' }
    $line = "sg={0,-3} sen={1,-3} gb={2,-4} | ol {3,5}({4,3}) 全网 {5,5}({6,3}) 期望 {7,4} 比值 {8,5} ol群体d' {9}" -f `
        $p.sg, $p.sen, $p.gb, $row.sig_ol, $row.ctl_ol, $row.sig_all, $row.ctl_all, $row.fp, $ratio, $row.ol_dp
    $line | Add-Content $log -Encoding UTF8
    Write-Output $line
}
"# 结束 $(Get-Date -Format 'HH:mm:ss')" | Add-Content $log -Encoding UTF8
