# Bedrock Client（Flarial風 HUDクライアント）

マイクラ統合版(Windows, DirectX11描画)に、ImGuiオーバーレイでHUDを表示するクライアントです。
ゲームメモリは読み書きせず、描画フックと入力ポーリングだけで動きます（チート機能なし）。

## 機能
FPS / CPS / Keystrokes / Clock、各モジュールのON/OFFと表示位置調整

## ビルド
- Visual Studio 2022 + CMake:  `cmake -B build -A x64 && cmake --build build --config Release`
- もしくはGitHubにpush → Actionsの Artifacts から DLL と injector.exe を取得

## 使い方
1. Minecraft Bedrock を起動（ビデオ設定でDX11。DX12/RTXモードだと非対応）
2. `injector.exe BedrockClient.dll` を実行（別exeを対象にする場合は第2引数にexe名: `injector.exe BedrockClient.dll Game.exe`、DX11のゲームのみ対応）
3. ゲーム内 `INSERT`: メニュー開閉（カーソルが出る画面で操作） / `END`: アンロード

## 注意
- マルチサーバーによっては注入自体をBAN対象にしている場合があります。サーバー規約を確認してください。
- ゲームのアップデートでも壊れにくい構造（オフセット不使用）ですが、描画APIが変わると動かなくなります。
- 今後の拡張案: 設定の保存(JSON)、Zoom、ArmorHUD(要メモリ解析)、テーマ、DX12対応
