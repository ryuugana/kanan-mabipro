# Makes MabiWiki_Standalone.html, the wiki as one file (the page players keep in their Mabinogi folder
# and Kanan opens), from the hostable page: this folder (the kanan repo's Kanan\MabiWiki, where the
# page is hostable.html and most of lib\ sits next to it) or a MabiWiki_Hostable folder (index.html,
# lib\). Kanan's build runs it (Kanan.vcxproj), writing Release\MabiWiki_Standalone.html.
#   powershell -NoProfile -ExecutionPolicy Bypass -File make-standalone.ps1 [-Out <file>] [-Src <folder>]
#
# It does exactly what make-standalone.html does in a browser (lib\make-standalone.js, whose top
# lists the steps), so both make the same file byte for byte: keep the two the same. In short: the
# hostable page's MABIWIKI_BASE "./" + MABIWIKI_HOSTED becomes MABIWIKI_BASE "MabiWiki/", data/site.js
# becomes data/build-info.js, the data scripts point into MabiWiki/data/, and app.css and every
# script go into the page ("</script" becoming "<\/script" so no block closes early).
# Works in Windows PowerShell 5.1 and PowerShell 7 (any system).
param(
    [string]$Out = 'MabiWiki_Standalone.html',
    [string]$Src = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding $false
$ignoreCase = [Text.RegularExpressions.RegexOptions]'IgnoreCase, CultureInvariant'

# A file by its path in the hostable folder ("lib/boot.js"), as a browser's File.text() reads it:
# UTF-8, a byte order mark dropped, line ends kept.
function Read-WikiFile([string]$path) {
    $names = if ($path -eq 'index.html') { @('hostable.html', 'index.html') } else { @($path, ($path -replace '^lib/', '')) }
    foreach ($name in $names) {
        $file = Join-Path $Src $name
        if (Test-Path -LiteralPath $file -PathType Leaf) { return [IO.File]::ReadAllText($file, $utf8) }
    }
    throw "No $path in $Src"
}

# A file's text as a script block that can't close early. "<!--" then "<script" (with no "-->"
# between) puts the HTML parser in its "double escaped" state, where the real </script> no longer
# closes the block: follow the parser's states and refuse such a file.
function Get-Block([string]$text, [string]$openTag) {
    $text = [regex]::Replace($text, '</(?=script)', '<\/', $ignoreCase)
    $state = 'data'
    foreach ($m in [regex]::Matches($text, '<!--|-->|<script[\s/>]', $ignoreCase)) {
        if ($m.Value -eq '-->') { $state = 'data' }
        elseif ($m.Value -eq '<!--') { if ($state -eq 'data') { $state = 'escaped' } }
        elseif ($state -eq 'escaped') { $state = 'double' }
    }
    if ($state -eq 'double') { throw 'A file ends inside "<!-- <script", so its block would not close.' }
    return $openTag + "`n" + $text + "`n</script>"
}

$script:page = Read-WikiFile 'index.html'
function Set-Once([string]$old, [string]$new) {
    $i = $script:page.IndexOf($old, [StringComparison]::Ordinal)
    if ($i -lt 0) { throw "index.html has no $old" }
    $script:page = $script:page.Substring(0, $i) + $new + $script:page.Substring($i + $old.Length)
}
function Plain([string]$name) { '<script type="text/plain" id="lib-' + $name + '">' }
function Run([string]$name) { '<script type="text/plain" data-run id="src-' + $name + '">' }
function Code([string]$name) { '<script id="src-' + $name + '">' }

Set-Once '<script>window.MABIWIKI_BASE = "./"; window.MABIWIKI_HOSTED = true;</script>' '<script>window.MABIWIKI_BASE = "MabiWiki/";</script>'
Set-Once '<script src="data/site.js"></script>' '<script src="data/build-info.js"></script>'
$script:page = $script:page.Replace('<script src="data/', '<script src="MabiWiki/data/')
# Last tag first, so a tag's text in code already put in can't be taken for the tag.
Set-Once '<script src="lib/boot.js"></script>' ((@(
    (Get-Block (Read-WikiFile 'lib/viewer3d.js') (Plain 'viewer3d')),
    (Get-Block (Read-WikiFile 'lib/region3d.js') (Plain 'region3d')),
    (Get-Block (Read-WikiFile 'lib/three-bundle.min.js') (Plain 'three')),
    (Get-Block (Read-WikiFile 'lib/three-LICENSE.txt') (Plain 'three-license')),
    (Get-Block (Read-WikiFile 'lib/packs.js') (Code 'packs')),
    (Get-Block (Read-WikiFile 'lib/images.js') (Code 'images')),
    (Get-Block (Read-WikiFile 'lib/sinks.js') (Code 'sinks')),
    (Get-Block (Read-WikiFile 'lib/prepare.js') (Code 'prepare')),
    (Get-Block (Read-WikiFile 'lib/boot.js') (Code 'boot'))
)) -join "`n")
Set-Once '<script type="text/plain" data-run src="app.js"></script>' (Get-Block (Read-WikiFile 'app.js') (Run 'app'))
Set-Once '<script type="text/plain" data-run src="datasource.js"></script>' (Get-Block (Read-WikiFile 'datasource.js') (Run 'datasource'))
Set-Once '<script src="sources.js"></script>' (Get-Block (Read-WikiFile 'sources.js') (Code 'sources'))
Set-Once '<link rel="stylesheet" href="app.css">' ('<style id="src-app-css">' + "`n" + (Read-WikiFile 'app.css') + "`n</style>")

$full = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }))
$dir = Split-Path $full
if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
[IO.File]::WriteAllText($full, $script:page, $utf8)
Write-Host "Wrote $full ($((Get-Item -LiteralPath $full).Length) bytes)"
