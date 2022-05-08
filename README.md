# Radiko player for ESP32  
ESP-ADF(https://github.com/espressif/esp-adf) のHTTP Stream pipelineを利用したラジコプレイヤーです。  
## 必要なもの  
ESP-ADF v2.4  
ESP-ADF対応ボード  
  
## ビルド方法  
ESP-IDF VS Code Extension(https://github.com/espressif/vscode-esp-idf-extension)  を使うと簡単です  
1. コマンドパレットから Configure ESP-IDF extensionを実行  
2. Expressをクリック  
3. インストール開始  
4. 完了後コマンドパレットからInstall ESP-ADFを実行  
5. 完了後ExtensionsサイドメニューからEspressif IDFの歯車アイコンをクリック Extension Settingsを開く
6. Esp Idf Path, Esp Idf Path WinをESP-ADFフォルダ内のESP-IDFフォルダに設定して再起動  
7. プロジェクトフォルダーをVS Codeで開く  
8. コマンドパレットからSet Espressif Device targetを実行してESP32を選択
9. コマンドパレットからSDK Configuration Editor(menuconfig)を開く 
10. Radiko Configurationからauth keyを探してRadikoの認証キーを入力(自力でなんとかしてください)  
12. Audio HAL内Audio board欄から使いたいボードを選択  
11. Example ConfigurationからWiFi SSID WiFi Passwordを探して入力、Saveボタンを押して閉じる
13. コマンドパレットからSelect port to useを実行してESP32の書き込み用ポートを設定する  
14. コマンドパレットからBuild flash, and start a monitorを実行
15. enjoy!  
  
## 操作方法  
#### LyraTほか:  
Playボタン:停止・再開  
Modeボタン: 選局  