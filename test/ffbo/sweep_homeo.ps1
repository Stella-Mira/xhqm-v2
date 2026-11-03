# 自稳 tau 扫描（DESIGN_NOTES 第二十节 待办 2 与 3）
#
# 判据（第 3 条）：tau 必须远大于刺激周期（两相窗口 1.5~2s）才能保住信号。
#   预期：tau=0.5s（远小于窗口）会把 looming 当漂移抹掉 ⇒ 显著数掉回假阳性期望；
#         tau=20s 应保住或提升；tau=2s（与窗口同量级）为边界。
# 判据（第 2 条）：跑 120s（tau=20 有 6 个时间常数），而不是原来只够 3 个的 60s。
#
# 记录：每类 [显著数 / 假阳性期望 / 均值活动 / 范围]，以及自稳是否真的在推。
$ErrorActionPreference = 'Continue'
$env:PATH = "C:\Qt\5.15.2\msvc2019_64\bin;D:\WorkSpace\xhqm_v2\binary;D:\WorkSpace\_research\digital-fruit-fly\depend\mujoco;D:\WorkSpace\xhqm_v2\depend\glew\bin;$env:PATH"
$exe  = "D:\WorkSpace\xhqm_v2\xhqm\test\ffbo\build\fly_mcns_view.exe"
$base = "D:\WorkSpace\_research\digital-fruit-fly\data\malecns"
$xml  = "D:\WorkSpace\_research\digital-fruit-fly\code\flygym\src\flygym\assets\model\flybody\fruitfly.xml"
$log  = "D:\WorkSpace\xhqm_v2\xhqm\test\ffbo\_sweep_homeo.log"

"# homeostat tau sweep  start $(Get-Date -Format 'HH:mm:ss')  sec=120" | Set-Content $log -Encoding UTF8
"# cols: tau | class | cells | sig | mean | min~max" | Add-Content $log -Encoding UTF8

$classes = @('ol_sensory','ol_intrinsic','visual_projection','cb_intrinsic','descending_neuron','vnc_intrinsic','vnc_motor')

foreach ($tau in 0, 0.5, 2, 20) {
    $a = @($base, $xml, '--sec', '120', '--predator', '--no-render', '--cell-model', 'bio',
           '--bio-graded', '--graded-base', '2.0', '--graded-amp', '1', '--norm-gain', '0',
           '--syn-gain', '16', '--loom-period', '5.333')
    if ($tau -gt 0) { $a += @('--homeo-tau', "$tau") }
    $t0 = Get-Date
    $o = & $exe @a 2>&1 | Out-String
    $mins = [math]::Round(((Get-Date) - $t0).TotalMinutes, 1)

    # 全网显著数与假阳性期望（该行以 "3" 与 "0.27%" 为特征，避开中文匹配）
    $mAll = [regex]::Match($o, '3\u03c3[^\d]*(\d+)[^\d]+.*?\u2248\s*(\d+)')
    if (-not $mAll.Success) { $mAll = [regex]::Match($o, '(\d+)\s*\u4e2a\uff1b.*?(\d+)\s*\u4e2a') }
    $tot = if ($mAll.Success) { $mAll.Groups[1].Value } else { '?' }
    $exp = if ($mAll.Success) { $mAll.Groups[2].Value } else { '?' }
    "tau=$tau  total_sig=$tot  fp_exp=$exp  wall=${mins}min" | Add-Content $log -Encoding UTF8

    foreach ($ln in ($o -split "`n")) {
        $tk = ($ln -replace '^\s+', '') -split '\s+'
        if ($tk.Count -lt 6) { continue }
        if ($classes -notcontains $tk[0]) { continue }
        # 班级名称 有发放 显著 群体d' CV 均值活动 min~max
        "    tau=$tau  $($tk[0])  cells=$($tk[1])  sig=$($tk[2])  cv=$($tk[4])  mean=$($tk[5])  range=$($tk[6])" | Add-Content $log -Encoding UTF8
    }
    Write-Output "tau=$tau done in ${mins}min : total_sig=$tot exp=$exp"
}
"# end $(Get-Date -Format 'HH:mm:ss')" | Add-Content $log -Encoding UTF8
