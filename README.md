[README.md](https://github.com/user-attachments/files/31601117/README.md)
# UE5-TDPlugins

TouchDesigner (TD) と Unreal Engine 5 (UE5) をリアルタイム連携させるための、2つのUE5プラグインをまとめたリポジトリです。

- **TD → UE5**: OSCでパラメータ（座標・回転・スケール・任意の数値）を送り、Actor/CineCameraをリアルタイムに駆動
- **UE5 → TD / TD → UE5**: Spoutで映像をやり取り（UE5のレンダリング結果をTDへ、TDの映像をUE5のマテリアルへ）

## 構成

```
UE5-TDPlugins/
├── UE-OSCActor-master/      # OSC受信 → Actor/Cameraパラメータ駆動プラグイン
└── UE-Spout2Media-master/   # Spout映像送受信プラグイン（Media Framework統合）
```

| プラグイン | ベース | 概要 |
|---|---|---|
| **OSCActor** | satoruhiga 氏の OSCActor | TDから送られるOSCメッセージをUE5内のActor/CineCameraへ反映。CineCamera対応やPIE時のクラッシュ修正などを追加でカスタマイズ |
| **Spout2Media** | BACKSPACE Productions Inc. の Spout2Media | UE5とTD間の映像をSpoutで送受信。Blueprintから直接扱える `SpoutSenderComponent` / `SpoutReceiverComponent` を追加でカスタマイズ |

## 主な機能

### OSCActor

- **`UOSCActorComponent`**: OSCで届いた値をキー指定で取得（`GetOSCParam` / `GetOSCMultiSampleParam`）、InstancedStaticMeshへの反映（`UpdateInstancedStaticMesh`）、更新時のBlueprintイベント通知（`UpdateFromOSC`）
- **`AOSCCineCameraActor` / `UOSCCineCameraComponent`**: OSCでカメラのTRS・FOVなどを駆動し、`CopyCameraSettingToSceneCaptureComponent2D` で `SceneCaptureComponent2D` に設定をコピー。エディタ・PIEどちらでもTickするため、Spout出力に常時反映される
- **`UOSCActorSubsystem`**: OSCサーバーの起動・受信・各コンポーネントへのディスパッチを担うEngineSubsystem。受信ポートやセンサーアスペクト比は Project Settings（`UOSCActorSettings`）から設定可能
- **`UOSCActorFunctionLibrary`**: 座標変換ユーティリティ（`FloatArrayToMatrix` / `TRSToMatrix` / GL→UE系座標変換）

### Spout2Media

- **`USpoutSenderComponent`**: `SceneCaptureComponent2D` のRenderTarget、またはビューポート全体をSpout経由で送信。Actorにアタッチするだけで、Blueprintから送信名やキャプチャモードを制御可能
- **`USpoutReceiverComponent`**: TD側のSpout Senderを受信し、指定したRenderTargetへ書き込み。接続成功時に発火する `OnConnected` デリゲートあり
- **`USpout2MediaOutput` / `USpout2MediaCapture` / `USpout2MediaSource` / `FSpout2MediaPlayer`**: UE5のMedia Framework（Media Output / Media Capture / Media Source / Media Player）にSpout規格を統合する低レベル実装

## 動作要件

- Unreal Engine 5（`Spout2MediaCapture` はUE5.4以降でAPIが分岐しているため、可能な限り新しいバージョンを推奨）
- Windows（Spout自体がWindows専用のため、Spout2MediaはWin64限定）
- TouchDesigner側でOSC Out CHOP／Spout TOP等を使った送受信設定

## セットアップ

1. `UE-OSCActor-master` と `UE-Spout2Media-master` を、UE5プロジェクトの `Plugins/` フォルダにコピー
2. `.uproject` を右クリック → *Generate Visual Studio project files*、またはUnreal Editorを起動してプラグインをビルド
3. *Edit > Plugins* で **OSCActor** と **Spout2Media** を有効化してエディタを再起動
4. *Project Settings > Plugins > OSCActor* で `OSCReceivePort`（既定値: `7000`）を設定
5. レベルに `AOSCActor` または `AOSCCineCameraActor` を配置し、`ObjectName` をTD側の送信アドレスと一致させる
6. 映像をTDへ送る場合は `SpoutSenderComponent` を、TDから映像を受け取る場合は `SpoutReceiverComponent` を対象のActorに追加

## クレジット

- **OSCActor** — original by [satoruhiga](https://github.com/satoruhiga)
- **Spout2Media** — original by BACKSPACE Productions Inc.

本リポジトリでは、UE5 × TouchDesignerを使った映像制作ワークフロー向けに両プラグインをカスタマイズしています。

## ライセンス

各プラグインのオリジナル実装のライセンスに準拠します。
