# Returns the private item keys from custom\private_items.txt (comments and blank lines skipped)
$file = Join-Path (Split-Path $PSScriptRoot) 'custom\private_items.txt'
$keys = @(Get-Content $file | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
if ($keys.Count -eq 0) { throw "No keys in $file" }
$keys
