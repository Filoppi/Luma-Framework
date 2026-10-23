param([Parameter(Mandatory=$true)][string]$ZigPath)
$ErrorActionPreference='Stop'
$compiler=(Get-Command $ZigPath).Source
$repoRoot=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..\..')).Path
$fix=Split-Path $PSScriptRoot -Parent
$out=Join-Path (Split-Path $repoRoot -Parent) 'luma-fixes-check'
New-Item -ItemType Directory -Force $out | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR=Join-Path $out 'cache-global'
$env:ZIG_LOCAL_CACHE_DIR=Join-Path $out 'cache-local'
$mh=Join-Path $repoRoot 'Source\External\reshade\deps\minhook\src'
$objects=@()
foreach($source in @((Join-Path $fix 'MouseFix_Settings.c'),(Join-Path $fix 'ParticleFix.c'),(Join-Path $mh 'buffer.c'),(Join-Path $mh 'hook.c'),(Join-Path $mh 'trampoline.c'),(Join-Path $mh 'hde\hde64.c'))){
 $obj=Join-Path $out ([IO.Path]::GetFileNameWithoutExtension($source)+'.o')
 & $compiler cc -target x86_64-windows-gnu -O2 -Wall -Wextra -c $source -o $obj
 if($LASTEXITCODE){throw "Compilation failed: $source"};$objects+=$obj
}
foreach($name in @('settings','registration','controls','particles','cutscenes')){
 $cpp=$name -eq 'cutscenes';$ext=if($cpp){'cpp'}else{'c'};$driver=if($cpp){'c++'}else{'cc'}
 $exe=Join-Path $out ('test-'+$name+'.exe')
 $testObjects=if($name -eq 'particles'){@()}else{$objects}
 & $compiler $driver -target x86_64-windows-gnu -O2 -Wall -Wextra -Wno-cast-function-type-mismatch (Join-Path $fix ('tests\test-'+$name+'.'+$ext)) @testObjects -luser32 -lkernel32 -o $exe
 if($LASTEXITCODE){throw "Compilation failed: $name"}
 & $exe
 if($LASTEXITCODE){throw "Test failed: $name"}
}
& $compiler c++ -target x86_64-windows-gnu -std=c++20 -Wno-unknown-attributes -c (Join-Path $fix 'tests\test-adapter.cpp') -o (Join-Path $out 'test-adapter.o')
if($LASTEXITCODE){throw 'Luma adapter compilation failed'}
& $compiler cc -target x86_64-windows-gnu -O2 -c (Join-Path $fix 'MouseFix.c') -o (Join-Path $out 'MouseFix.o')
if($LASTEXITCODE){throw 'Integrated input module compilation failed'}
