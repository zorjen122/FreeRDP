$env:OPENSSL_ROOT_DIR="$env:VCPKG_ROOT\installed\x64-windows"

# 重新构建 FreeRDP
# 重点：添加了 WITH_INTERNAL_MD4, WITH_INTERNAL_MD5, WITH_INTERNAL_RC4 以修复授权错误
cmake -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DWITH_OPENSSL=ON `
    -DOPENSSL_USE_STATIC_LIBS=OFF `
    -DWITH_INTERNAL_MD4=ON `
    -DWITH_INTERNAL_MD5=ON `
    -DWITH_INTERNAL_RC4=ON `
    -DWITH_CLIENT_WINDOWS=ON `
    -DWITH_CLIENT_SAMPLE=ON `
    -DWITH_SERVER=OFF `
    -DCHANNEL_URBDRC=OFF `
    -DCHANNEL_RDP2VIR=ON `
    -DWITH_SMARTCARD_EMULATE=OFF `
    -DWITH_SWSCALE=OFF `
    -DWITH_FFMPEG=OFF `
    -DWITH_SWRESAMPLE=OFF `
    -DWITH_LIBAV=OFF `
    -DWITH_GFX=OFF `
    -DWITH_WIN_CONSOLE=ON `
    -DWITH_VERBOSE_WINPR_ASSERT=OFF `
    ..

Write-Host "配置已完成：已添加内置 RC4/MD4 支持以修复 Licensing 错误" -ForegroundColor Green
Write-Host "现在运行: cmake --build . --config Release" -ForegroundColor Yellow