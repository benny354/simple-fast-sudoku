param([switch]$Test, [int]$PuzzleCount = 1000)
$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
$compilerDir = Join-Path $env:LOCALAPPDATA 'SudokuNativeTools'
$compiler = Join-Path $compilerDir 'tcc\tcc.exe'

if (-not (Test-Path -LiteralPath $compiler)) {
    New-Item -ItemType Directory -Path $compilerDir -Force | Out-Null
    $archive = Join-Path $compilerDir 'tcc-0.9.27-win64-bin.zip'
    Write-Host '正在下载轻量 C 编译器（仅首次编译需要）...'
    Invoke-WebRequest -Uri 'https://download.savannah.gnu.org/releases/tinycc/tcc-0.9.27-win64-bin.zip' -OutFile $archive
    $expected = '34A721949A2583FDFF725312DA092FA0F5F1F284B702E6F811C6954714FAABB2'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
        throw '编译器校验失败，已停止。'
    }
    Expand-Archive -LiteralPath $archive -DestinationPath $compilerDir -Force
}

# Windows' own resource API embeds the icon and DPI manifest; no resource SDK needed.
if (-not ('SudokuBuild.NativeResources' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
namespace SudokuBuild {
    public static class NativeResources {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern IntPtr BeginUpdateResource(string file, bool deleteExisting);
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern bool UpdateResource(IntPtr handle, IntPtr type, IntPtr name, ushort lang, byte[] data, uint size);
        [DllImport("kernel32.dll", SetLastError=true)]
        static extern bool EndUpdateResource(IntPtr handle, bool discard);
        static void Put(IntPtr handle, int type, int name, byte[] data) {
            if (!UpdateResource(handle, (IntPtr)type, (IntPtr)name, 0, data, (uint)data.Length))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        static byte[] Block(string key, ushort type, byte[] value, ushort valueLength, params byte[][] children) {
            using (MemoryStream stream=new MemoryStream()) {
                using (BinaryWriter writer=new BinaryWriter(stream)) {
                    writer.Write((ushort)0); writer.Write(valueLength); writer.Write(type);
                    writer.Write(Encoding.Unicode.GetBytes(key+"\0"));
                    while (stream.Position%4!=0) writer.Write((byte)0);
                    writer.Write(value);
                    foreach (byte[] child in children) {
                        while (stream.Position%4!=0) writer.Write((byte)0);
                        writer.Write(child);
                    }
                    long length=stream.Length;
                    stream.Position=0; writer.Write((ushort)length);
                    return stream.ToArray();
                }
            }
        }
        static byte[] VersionText(string key, string value) {
            return Block(key,1,Encoding.Unicode.GetBytes(value+"\0"),(ushort)(value.Length+1));
        }
        static byte[] VersionInfo() {
            byte[] fixedInfo;
            using (MemoryStream stream=new MemoryStream()) {
                using (BinaryWriter writer=new BinaryWriter(stream)) {
                    uint[] fields={0xFEEF04BDu,0x00010000u,0x00010001u,0u,0x00010001u,0u,
                                   0x3Fu,0u,0x00040004u,1u,0u,0u,0u};
                    foreach (uint field in fields) writer.Write(field);
                    fixedInfo=stream.ToArray();
                }
            }
            byte[] strings=Block("StringFileInfo",1,new byte[0],0,
                Block("080404b0",1,new byte[0],0,
                    VersionText("FileDescription","一个简单快速的数独"),
                    VersionText("FileVersion","1.1.0.0"),
                    VersionText("ProductName","一个简单快速的数独"),
                    VersionText("ProductVersion","1.1.0"),
                    VersionText("OriginalFilename","一个简单快速的数独.exe")));
            byte[] variables=Block("VarFileInfo",1,new byte[0],0,
                Block("Translation",0,new byte[]{0x04,0x08,0xB0,0x04},4));
            return Block("VS_VERSION_INFO",0,fixedInfo,52,strings,variables);
        }
        public static void Embed(string file, byte[] icon, byte[] manifest) {
            IntPtr handle=BeginUpdateResource(file,false);
            if (handle==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            bool ready=false;
            try {
                int count=BitConverter.ToUInt16(icon,4);
                byte[] group=new byte[6+14*count];
                Buffer.BlockCopy(icon,0,group,0,6);
                for (int i=0;i<count;i++) {
                    int entry=6+16*i;
                    int size=BitConverter.ToInt32(icon,entry+8), offset=BitConverter.ToInt32(icon,entry+12);
                    byte[] data=new byte[size];
                    Buffer.BlockCopy(icon,offset,data,0,size);
                    Put(handle,3,i+1,data);
                    Buffer.BlockCopy(icon,entry,group,6+14*i,12);
                    Buffer.BlockCopy(BitConverter.GetBytes((ushort)(i+1)),0,group,6+14*i+12,2);
                }
                Put(handle,14,1,group);
                Put(handle,24,1,manifest);
                Put(handle,16,1,VersionInfo());
                ready=true;
            } finally {
                if (!EndUpdateResource(handle,!ready) && ready)
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }
    }
}
'@
}

Push-Location -LiteralPath $projectDir
try {
    New-Item -ItemType Directory -Path 'build' -Force | Out-Null
    $temporaryExe = Join-Path $projectDir 'build\sudoku.exe'
    & $compiler -Wall '-Wl,-subsystem=windows' -o $temporaryExe 'src\main.c' 'src\sudoku.c' -luser32 -lgdi32 -lkernel32
    if ($LASTEXITCODE -ne 0) { throw '游戏编译失败。' }
    [SudokuBuild.NativeResources]::Embed($temporaryExe,
        [IO.File]::ReadAllBytes((Join-Path $projectDir 'src\app.ico')),
        [IO.File]::ReadAllBytes((Join-Path $projectDir 'src\app.manifest')))
    $outputExe = Join-Path $projectDir '一个简单快速的数独.exe'
    Move-Item -LiteralPath $temporaryExe -Destination $outputExe -Force
    Write-Host ('已生成：{0}（{1:N1} KB）' -f $outputExe, ((Get-Item -LiteralPath $outputExe).Length / 1KB))
    if ($Test) {
        & $compiler -Wall -o 'build\core_test.exe' 'tests\core_test.c' 'src\sudoku.c'
        if ($LASTEXITCODE -ne 0) { throw '测试编译失败。' }
        & '.\build\core_test.exe' $PuzzleCount
        if ($LASTEXITCODE -ne 0) { throw '数独测试失败。' }
        & $compiler -Wall -o 'build\game_test.exe' 'tests\game_test.c' 'src\sudoku.c' -luser32 -lgdi32 -lkernel32
        if ($LASTEXITCODE -ne 0) { throw '交互逻辑测试编译失败。' }
        & '.\build\game_test.exe'
        if ($LASTEXITCODE -ne 0) { throw '交互逻辑测试失败。' }
    }
} finally {
    Pop-Location
}
