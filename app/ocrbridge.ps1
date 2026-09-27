# EditHere OCR bridge for Windows.
#
# Recognises the text in one PNG using the optical character recogniser that is
# part of Windows (Windows.Media.Ocr). This is a script rather than C++ because
# the application is built with MinGW GCC as well as MSVC, and the official
# C++/WinRT headers only compile with MSVC, while Windows PowerShell 5.1 can use
# the same WinRT types through their language projection.
#
# How the application runs it: the script is embedded as a Qt resource, the three
# placeholders below are replaced, the result is base64 encoded as UTF-16LE and
# handed to
#     powershell.exe -NoProfile -NonInteractive -EncodedCommand <base64>
# so nothing has to be written to disk and the execution policy is not involved.
# The answer goes to a file instead of stdout, because a pipe changes how
# PowerShell encodes its console output and the recognised text is not ASCII.
#
# Input  (placeholders, replaced before encoding):
#   __EDITHERE_IMAGE__     PNG to read
#   __EDITHERE_OUTPUT__    JSON to write
#   __EDITHERE_LANGUAGE__  preferred BCP-47 tag, empty for the user's languages
# Output: {"engineLanguage":"<tag>","lines":[{"text":"..","x":0,"y":0,"w":0,"h":0}]}
# The box of a line is in pixels of the image that was read.
# Exit codes: 0 ok, 1 failure (message on stderr), 2 image missing,
#             3 no OCR engine for any installed language.
#
# Keep this file ASCII-only and without a byte order mark: it is never read from
# disk by PowerShell, so a mark would travel inside the encoded command text.
$ErrorActionPreference = 'Stop'
# Progress records are serialised to the error stream as CLIXML when there is no
# console, which buries the real message in noise.
$ProgressPreference = 'SilentlyContinue'
$path = '__EDITHERE_IMAGE__'
$out = '__EDITHERE_OUTPUT__'
$wanted = '__EDITHERE_LANGUAGE__'
# Quotes and backslashes are escaped so the message survives the trip through JSON.
function Format-Detail($message) {
    $text = [string]$message
    $text = $text.Replace('\', '\\').Replace('"', '\"')
    $text = $text -replace "`r", ' ' -replace "`n", ' ' -replace "`t", ' '
    return $text
}
try {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        [Console]::Error.WriteLine('image-not-found')
        exit 2
    }
    # Provides the task-based projection of IAsyncOperation<T>.
    $null = Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $engineType = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
    $languageType = [Windows.Globalization.Language, Windows.Foundation, ContentType = WindowsRuntime]
    $fileType = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
    $decoderType = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
    # A Windows Runtime enum has to be named with its assembly before it can be used.
    $accessMode = [Windows.Storage.FileAccessMode, Windows.Storage, ContentType = WindowsRuntime]
    $streamType = [Windows.Storage.Streams.IRandomAccessStream]
    $bitmapType = [Windows.Graphics.Imaging.SoftwareBitmap]
    $resultType = [Windows.Media.Ocr.OcrResult]
    # AsTask is an extension method of the projection, so it is located by
    # signature and closed over the result type by hand.
    $asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
            $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        })[0]
    function Invoke-Await($operation, $type, $step) {
        $task = $asTask.MakeGenericMethod($type).Invoke($null, @($operation))
        try {
            $null = $task.Wait(-1)
        } catch {
            # Task.Wait wraps whatever went wrong in an AggregateException, and
            # PowerShell wraps that in a MethodInvocationException, so the deepest
            # one is the only part worth reading. The step name says where it
            # happened, because none of the calls says so itself.
            $inner = $_.Exception
            while ($null -ne $inner.InnerException) {
                $inner = $inner.InnerException
            }
            throw ($step + ': ' + $inner.GetType().Name + ': ' + $inner.Message)
        }
        return $task.Result
    }
    function Find-Engine {
        if ($wanted -ne '') {
            $named = $engineType::TryCreateFromLanguage($languageType::new($wanted))
            if ($null -ne $named) { return $named }
        }
        $profile = $engineType::TryCreateFromUserProfileLanguages()
        if ($null -ne $profile) { return $profile }
        # An English-only installation still reads Latin text.
        return $engineType::TryCreateFromLanguage($languageType::new('en-US'))
    }
    $engine = Find-Engine
    if ($null -eq $engine) {
        [Console]::Error.WriteLine('no-ocr-engine')
        exit 3
    }
    $file = Invoke-Await ($fileType::GetFileFromPathAsync($path)) ([Windows.Storage.StorageFile]) 'open-file'
    $stream = Invoke-Await ($file.OpenAsync($accessMode::Read)) ($streamType) 'open-stream'
    $decoder = Invoke-Await ($decoderType::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder]) 'decode'
    $bitmap = Invoke-Await ($decoder.GetSoftwareBitmapAsync()) ($bitmapType) 'bitmap'
    $result = Invoke-Await ($engine.RecognizeAsync($bitmap)) ($resultType) 'recognize'
    # ConvertTo-Json unwraps a single element array, so every record is encoded on
    # its own (an object is never unwrapped) and the array is assembled by hand.
    $records = @()
    foreach ($line in $result.Lines) {
        $words = @($line.Words)
        if ($words.Count -eq 0) { continue }
        $left = [double]::MaxValue
        $top = [double]::MaxValue
        $right = [double]::MinValue
        $bottom = [double]::MinValue
        foreach ($word in $words) {
            $rect = $word.BoundingRect
            if ($rect.X -lt $left) { $left = $rect.X }
            if ($rect.Y -lt $top) { $top = $rect.Y }
            if (($rect.X + $rect.Width) -gt $right) { $right = $rect.X + $rect.Width }
            if (($rect.Y + $rect.Height) -gt $bottom) { $bottom = $rect.Y + $rect.Height }
        }
        $records += ConvertTo-Json -Compress -InputObject ([pscustomobject]@{
                text = $line.Text
                x = [int][Math]::Round($left)
                y = [int][Math]::Round($top)
                w = [int][Math]::Round($right - $left)
                h = [int][Math]::Round($bottom - $top)
            })
    }
    $tag = $engine.RecognizerLanguage.LanguageTag
    $tag = $tag.Replace('\', '\\').Replace('"', '\"')
    $json = '{"engineLanguage":"' + $tag + '","lines":[' + ($records -join ',') + ']}'
    [IO.File]::WriteAllText($out, $json, [Text.UTF8Encoding]::new($false))
    $stream.Dispose()
    exit 0
} catch {
    # The message is written to the output file as well as the error stream: the
    # error stream arrives as CLIXML in whatever code page the console uses, and
    # the application needs the text in a form it can read and log.
    $detail = $_.Exception.GetType().Name + ': ' + $_.Exception.Message
    try {
        $failure = '{"error":"' + (Format-Detail $detail) + '"}'
        [IO.File]::WriteAllText($out, $failure, [Text.UTF8Encoding]::new($false))
    } catch {
        # Nothing left to report with: the error stream is all there is.
    }
    [Console]::Error.WriteLine('recognition-failed')
    exit 1
}
