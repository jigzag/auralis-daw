# Auralis 状態遷移の監査

対象は **0.4.44 / `5ee7ecc`** の Auralis 本体 (`Source/`)。画面、コマンド、保存、オーディオコールバック、内蔵エフェクトを含む。JUCE・LAME・外部VSTの内部は各依存先の責任範囲とし、Auralisとの接点を対象にした。配布スクリプトはアプリ本体とは別に末尾で扱う。

### 0.4.45で対応した項目

| 項目 | 実装と確認箇所 |
|---|---|
| TR-03 | `Engine::finishRecording` は全音声 writer の解放を先に済ませ、その後に各テイクを取り込む。`RecordingResult` に追加数、失敗したトラック、残っている音声ファイルを記録する。`RecordingRangeTests` は途中の読み込み失敗、正常テイクの追加、再録音、重複確定の防止を検証する。 |
| DOC-03 | `Engine::loadProject` は一時セッションの準備後、旧プラグインを入れ替える前にエディターを閉じる。`ProjectOpenTests` は準備失敗時に元のエディターを保持し、正常時に閉じることを検証する。 |
| APP-05／06 | 処理名と完了結果は `Workstation::taskState()` で取得できる。録音と非同期処理の完了通知は `setStatus` を通る。`ProjectOpenTests` と `RecordingRangeTests` は診断カウンターが残った状態の通知と、反映拒否からの復帰を検証する。 |
| TR-02／04、TL-01、PITCH-02 | `Engine::recordingPhase()`、`Workstation::hasActiveRange()` と `canClearRange()`、`Timeline::interaction()`、`PitchRollGrid::interaction()` を読み取り専用の確認点にした。既存の操作経路から呼び出すC++ APIであり、エージェント専用の外部操作APIは追加していない。 |

入力が同じでも以前の操作、保留中のコールバック、選択、音声処理の履歴などによって結果が変わる箇所を状態機械として数えた。単独の設定値や表示だけを持つ変数は、関連する機械の入力に含めた。以下のIDは監査用の識別子で、アプリの公開APIではない。

## 状態機械の一覧

| ID | 持ち主・入口 | 状態と遷移、重要な条件 | 既存の確認箇所 |
|---|---|---|---|
| APP-01 起動・終了 | [Main.cpp](../Source/Main.cpp) | 引数から通常画面／セルフテスト／VSTスキャン子プロセス／撮影へ分岐。通常の終了は作業中・録音・未適用音程・保存確認を通る。 | `ProjectOpenTests`、セルフテスト入口 |
| APP-02 デバイス | [Engine.cpp](../Source/Engine.cpp) `initialise`, `audioDeviceAboutToStart`, `audioDeviceStopped` | 設定復元 → 準備 → コールバック稼働 → 停止・再準備。録音中の停止は中断フラグをGUIタイマーへ渡す。サンプルレートとバッファ変更でDSPを再準備。 | `RealtimeTests`、`RecordingAlignmentTests`。実機は別検証 |
| APP-03 作業領域 | [ProjectStart.cpp](../Source/ProjectStart.cpp)、[Interface.cpp](../Source/Interface.cpp) | 開始画面／編集画面。開始画面を表示すると再生が止まる。Mixerと詳細欄は独立して開閉。選択・カットの状態は維持される経路がある。 | `RecentProjectTests`、`MixerTests`、`UxTests` |
| APP-04 入力欄の編集 | [Interface.cpp](../Source/Interface.cpp)、[TrackDetails.cpp](../Source/TrackDetails.cpp)、[Mixer.cpp](../Source/Mixer.cpp) | 表示同期 → フォーカス・ドラッグ中 → 確定。タイマーは入力中の値を上書きしない。丸めた表示を再確定しても値を変更しない経路がある。 | `ProjectOpenTests`、`TrackDetailsTests`、`MixerTests`、`CutFocusTests` |
| APP-05 処理中 | [Interface.cpp](../Source/Interface.cpp) `runBackground`、[PitchRollHost.cpp](../Source/PitchRollHost.cpp)、[PitchInterface.cpp](../Source/PitchInterface.cpp) | 待機 → 音声書き出し／VSTスキャン／音程解析／音程補正 → 成功・失敗 → UI再有効化。`busy` とワーカーとGUIコールバックが状態を分担。 | `RangeExportTests`、`PitchRollTests`、`PitchCorrectionTests`、`PluginRegistryTests` |
| APP-06 通知 | [Interface.cpp](../Source/Interface.cpp) `setStatus`, `error`, `timerCallback` | 操作メッセージの保護期間 → 診断を優先可能。処理中やエラーは別経路でも通知される。診断カウンターはリセット可能。 | `UxTests`、`RealtimeTests` |
| APP-07 ダイアログと遅延操作 | [Interface.cpp](../Source/Interface.cpp)、[Normalize.cpp](../Source/Normalize.cpp)、[PitchInterface.cpp](../Source/PitchInterface.cpp)、[TrackDetails.cpp](../Source/TrackDetails.cpp) | メニュー／入力欄／ファイル選択 → 確定・取消 → 遅延結果の適用。所有者の消滅を `SafePointer` で検査し、クリップ・トラック・インサートのIDを再解決する経路がある。 | `ProjectOpenTests`、`TrackDeleteTests`、`ClipGainTests`、`NormalizeTests`、`MasterInsertTests` |
| DOC-01 プロジェクトの変更 | [Interface.cpp](../Source/Interface.cpp) `changed`, `markProjectSaved`, `pluginStateSnapshot` | 新規／保存済み／変更あり。`dirty` に加えてプラグイン状態の保存時スナップショットを終了時に比較し、音程の未適用変更を別に確認する。 | `ProjectOpenTests`、`PitchRollTests` |
| DOC-02 新規・開く・閉じる | [Interface.cpp](../Source/Interface.cpp) `requestClose`, `save`, `openSelectedProject`、[ProjectStart.cpp](../Source/ProjectStart.cpp) | 開くファイルを選ぶ → 保存確認 → 保存／破棄／取消 → 元の要求を続行。保存先選択の取消や保存失敗では続行しない。 | `ProjectOpenTests`、`RecentProjectTests` |
| DOC-03 読み込みと承認 | [ProjectSafety.cpp](../Source/ProjectSafety.cpp) `ProjectDocument::read`、[Engine.cpp](../Source/Engine.cpp) `loadProject`、[Interface.cpp](../Source/Interface.cpp) `installProject` | 検証・承認 → 一時Engineで素材とプラグインを準備 → 音声セッションを入れ替え。失敗時に元のEngineを維持。エディターのクローズは現状その前に発生する。 | `ProjectSafetyTests`、`ProjectOpenTests` |
| DOC-04 保存・履歴 | [Engine.cpp](../Source/Engine.cpp) `saveProject`、[ProjectStart.cpp](../Source/ProjectStart.cpp) | 素材をコピー → プロジェクト保存 → 保存済み状態を更新 → 最近使った一覧を永続化。履歴にあるファイルは存在／移動済み・削除済み／一覧から除外。履歴保存の失敗はプロジェクト保存と別扱い。 | `ProjectOpenTests`、`RecentProjectTests`、`ProjectSafetyTests` |
| DOC-05 編集履歴 | [Interface.cpp](../Source/Interface.cpp) `rememberEdit`, `undoEdit`、[PitchRoll.cpp](../Source/PitchRoll.cpp) | メインはクリップ配列のスナップショットを戻す。操作によって履歴をクリアする。音程エディターはノートのローカルUndo/Redoを持ち、適用後はメインへ移る。 | `CutTests`、`ClipGainTests`、`PianoEditTests`、`PitchCurveTests` |
| DOC-06 素材読み込み | [Interface.cpp](../Source/Interface.cpp) `importFiles`、[Engine.cpp](../Source/Engine.cpp) `importAudio`、[Midi.cpp](../Source/Midi.cpp) `importMidi` | 読み込み元と配置先を決める → 入力を順に変換・追加 → 完了または途中失敗。複数ファイルは一括巻き戻しの単位ではない。 | `MidiTests`、`TrackDetailsTests`、セルフテストの入出力 |
| TR-01 再生・位置 | [Engine.cpp](../Source/Engine.cpp) `play`, `stop`, `seek`, `renderChunk`、[Interface.cpp](../Source/Interface.cpp) | 停止／再生／シーク。Stopから録音確定へ進む場合がある。シーク・停止・ループ折り返しはMIDIの連続性を切り替える。GUIはフラグを読み表示と操作を同期。 | `RealtimeTests`、`TimelineLaneTests`、`LoopTests` |
| TR-02 録音全体 | [Engine.cpp](../Source/Engine.cpp) `startRecordingWriters`, `renderChunk`, `finishRecording`、[Interface.cpp](../Source/Interface.cpp) `timerCallback` | 停止／再生 → 準備 → カウントイン → プリロール → 収録 → 範囲終端・停止・デバイス中断 → 確定。範囲完了の直後は `recording && !playing && recordingComplete` が正当。 | `RecordingRangeTests`、`RecordingAlignmentTests`、`UxTests` |
| TR-03 テイクの確定 | [Engine.cpp](../Source/Engine.cpp) `finishRecording`、[Midi.cpp](../Source/Midi.cpp) `finishMidiCapture` | トラック別に writer／MIDIバッファを確保 → サンプル・イベントを収録 → ファイルを閉じて読み込み → クリップ追加。空のテイクと途中失敗の扱いがある。 | `RecordingRangeTests`、`RecordingWaveformTests`、`TrackControlTests` |
| TR-04 範囲と適用先 | [Interface.cpp](../Source/Interface.cpp) `setTimeSelection`, `clearTimeSelection`, `togglePunchSelection`, `toggleLoopSelection`, `defaultExportRange` | 空／選択中の時間範囲と、録音・ループの有効フラグおよび保存済み端点。メインの解除は選択と両モードを解除。書き出しの初期値は青い選択。 | `RangeExportTests`、`LoopTests`、`FixedRulerTests`、`UxTests` |
| TR-05 ループ所有権 | [PitchLoop.cpp](../Source/PitchLoop.cpp)、[Engine.cpp](../Source/Engine.cpp) `render` | プロジェクトループと音程エディター一時ループ。エディターの所有者IDで更新・解除を制限し、録音・書き出しでは一時ループを使わない。 | `LoopTests`、`PitchLoopTests`、`UxTests` |
| TR-06 書き出し | [Interface.cpp](../Source/Interface.cpp) `exportAudio`、[Engine.cpp](../Source/Engine.cpp) `exportMix`、[Midi.cpp](../Source/Midi.cpp) `exportMidi` | 範囲・形式 → 保存先 → 準備 → FX履歴用プリロール → 生成 → 仮ファイルから確定、または失敗時の後始末。オフライン処理の終了時にEngineの位置・再生フラグ等を戻す。GUIは書き出し前に停止させる。 | `RangeExportTests`、`SpaceEffectsTests`、`MidiTests` |
| TR-07 再生追従 | [Interface.cpp](../Source/Interface.cpp) `followTransport`、[PitchRoll.cpp](../Source/PitchRoll.cpp) `followTransport` | OFF／追従／手動スクロール後に一時停止。ドラッグ・試聴・範囲・カウントインなどの条件で追従を抑止。 | `FollowTests`、`PitchFollowTests` |
| TR-08 メトロノームと録音波形 | [Engine.cpp](../Source/Engine.cpp) `renderChunk`, `RecordingWaveform::append` | 停止／クリック／カウントインの音と、未収録／収録済みサンプルの表示。カウント音を入力録音・書き出しから分離。 | `RecordingRangeTests`、`RecordingWaveformTests`、セルフテストのメトロノーム |
| MIX-01 ルーティング | [Engine.cpp](../Source/Engine.cpp) `renderChunk`、[Interface.cpp](../Source/Interface.cpp) `toggleTrackControl` | REC・MUTE・SOLO・MONは独立。MONは音声トラックのクリップ再生を入力へ切り替え、オフライン書き出しとは経路が異なる。MIDIトラックと音声トラックの入力権限も異なる。 | `TrackControlTests`、`MixerTests`、`RealtimeTests` |
| MIX-02 チャンネル選択と表示 | [Mixer.cpp](../Source/Mixer.cpp)、[Interface.cpp](../Source/Interface.cpp) `selectTrack`、[TrackDetails.cpp](../Source/TrackDetails.cpp) | トラック選択／MIDI入力先／ミキサー表示の同期。トラック削除や並べ替えの後はIDで対応するトラックを再解決。画面外の選択はミキサー操作から表示を移動。 | `MixerTests`、`TrackDragTests`、`TrackDeleteTests` |
| MIX-03 重なりの発音優先 | [Engine.cpp](../Source/Engine.cpp) `buildClipOwners`、[Interface.cpp](../Source/Interface.cpp) `clipDrawOrder`, `trimEdgeAt` | 時間区間ごとに最新パートを採用。見えているパートと選択した背面パートの編集対象は別。分割・トリム・移動・保存で順序を保持。 | `OverlapTests`、`OverlapTrimTests` |
| TL-01 タイムラインの操作 | [Interface.cpp](../Source/Interface.cpp) `Timeline::mouseDown`, `mouseDrag`, `mouseUp` | Idle／再生位置移動／時間範囲ドラッグ／パート移動・トリム・音量変更／トラック移動・高さ変更。モードとマウス位置で入口が分岐し、最初の変更時にUndoを記録する。 | `TimelineLaneTests`、`ClipGainTests`、`ClipTrimTests`、`TrackDragTests`、`FixedRulerTests` |
| TL-02 選択／ツール／折りたたみ | [Interface.cpp](../Source/Interface.cpp) `setCutMode`, `keyPressed`、[TrackDetails.cpp](../Source/TrackDetails.cpp) | 選択ツール／カット、パート未選択／選択、トラックの縮小／前の高さに復帰。各状態は独立。フォーカスが入力欄にあるときは編集ショートカットを譲る。 | `CutFocusTests`、`TrackDragTests`、`TrackDeleteTests` |
| MIDI-01 ピアノロール | [PianoRoll.cpp](../Source/PianoRoll.cpp) | 選択・矩形選択 → 移動／リサイズ → 確定。矩形の拍範囲は連続複製の刻みにも使う。固定ルーラーとロールのシークフラグ、クリップIDの再解決。 | `PianoEditTests`、セルフテストのピアノロール操作 |
| MIDI-02 試聴と送信 | [PianoRoll.cpp](../Source/PianoRoll.cpp) `endPreview`、[Midi.cpp](../Source/Midi.cpp) `selectMidiTrack`, `buildTrackMidi`、[Engine.cpp](../Source/Engine.cpp) | キー押下 → Note On → アップ／フォーカス喪失／非表示／無効化でNote Off。トラック変更・シーク・ループ等の再同期で内蔵音源／VST／外部MIDIを整える。保持中ノートと録音イベントは別。 | `MidiTests`、`RealtimeTests`、`LoopTests` |
| PITCH-01 解析と編集セッション | [PitchRollHost.cpp](../Source/PitchRollHost.cpp)、[PitchRoll.cpp](../Source/PitchRoll.cpp)、[PitchInterface.cpp](../Source/PitchInterface.cpp) | 解析中 → 編集可能 → 未適用／素材変更で古い状態 → 適用／変更破棄／再解析。完了時に元ソース・範囲と対象IDを照合。保存・書き出し・終了には適用または破棄が必要。 | `PitchRollTests`、`PitchCorrectionTests`、`PitchCurveTests` |
| PITCH-02 ジェスチャーと編集値 | [PitchRollGrid.cpp](../Source/PitchRollGrid.cpp)、[PitchCurve.cpp](../Source/PitchCurve.cpp)、[PitchRoll.cpp](../Source/PitchRoll.cpp)、[PitchScoop.cpp](../Source/PitchScoop.cpp) | 矩形選択／ノート移動／カーブのストロークを処理。ブロック外の検出音は一時領域を作り、確定・取消で反映を決める。数値・しゃくりダイアログには試聴と確定／取消がある。 | `PitchCurveTests`、`PitchScoopTests`、`PitchZoomTests` |
| PITCH-03 シーク・ループ編集 | [PitchRoll.cpp](../Source/PitchRoll.cpp)、[PitchLoop.cpp](../Source/PitchLoop.cpp) | シーク中は正規化した編集位置を保持。ループ範囲は作成・端点変更・取消・解除があり、非表示時はエディターの一時ループを解除する。 | `PitchLoopTests`、`PitchPreviewTests`、`PitchFollowTests` |
| PITCH-04 ドラッグ試聴 | [PitchPreview.cpp](../Source/PitchPreview.cpp)、[PitchRoll.cpp](../Source/PitchRoll.cpp) | 停止 → 所有者付き試聴 → リリースフェード → 通常の再生へ復帰。録音・書き出し・対象削除・無効化で終了。表示上の試聴とエンジンの実際の所有者を区別。 | `PitchPreviewTests`、`PitchCurveTests` |
| PITCH-05 ピッチ検出とレンダリング | [PitchProcess.cpp](../Source/PitchProcess.cpp)、[PitchSegments.cpp](../Source/PitchSegments.cpp) | 有声／無声から音符候補をまとめる。対象編集・自動補正の切り替え、補正範囲の前後の遷移、生成後の元音源への参照を管理。短い検出片はノート枠と独立して編集可能。 | `PitchCorrectionTests`、`PitchCurveTests`、`PitchScoopTests` |
| FX-01 スキャンと登録 | [Interface.cpp](../Source/Interface.cpp) `scanPlugins`, `loadPluginRegistry`, `savePluginRegistry`、[Main.cpp](../Source/Main.cpp) | 選択 → 候補列挙 → 子プロセス → 成功・失敗・タイムアウト → XML取り込み → レジストリ保存。認識結果と永続化の成功は別。 | `PluginRegistryTests`、`PluginInfoTests`、セルフテストの子プロセス |
| FX-02 インサートとエディター | [TrackDetails.cpp](../Source/TrackDetails.cpp)、[Interface.cpp](../Source/Interface.cpp)、[Engine.cpp](../Source/Engine.cpp) | 追加 → インスタンス準備 → エディター表示／非表示・バイパス → 削除。マスターとトラックをIDで識別し、インスタンスの破棄前にエディターを閉じる必要がある。 | `MasterInsertTests`、`TrackDetailsTests`、`PluginInfoTests`、`ProjectOpenTests` |
| FX-03 内蔵EQ | [Equalizer.cpp](../Source/Equalizer.cpp)、[EqualizerEditor.cpp](../Source/EqualizerEditor.cpp) | 準備 → フィルター履歴更新 → リセット。バンド選択とグラフのドラッグ、バンドON／OFFと全体バイパス、係数・出力の遷移。 | `EqualizerTests` |
| FX-04 内蔵コンプレッサー | [Compressor.cpp](../Source/Compressor.cpp)、[CompressorEditor.cpp](../Source/CompressorEditor.cpp) | 準備・リセット → アタック／リリース → ゲインとメーター表示。バイパスでウェット量を遷移。 | `NativeDynamicsTests` |
| FX-05 内蔵チューナー | [Tuner.cpp](../Source/Tuner.cpp)、[TunerEditor.cpp](../Source/TunerEditor.cpp) | 準備／入力監視／ワーカー解析 → 検出／信号なし → 停止・リセット。epochで古い解析結果を無効化。ミュートは検出の停止と独立。 | `NativeDynamicsTests` |
| FX-06 内蔵リバーブ・ディレイ | [SpaceEffects.cpp](../Source/SpaceEffects.cpp)、[SpaceEffectsEditor.cpp](../Source/SpaceEffectsEditor.cpp) | 準備／リセット → 履歴バッファ更新 → 残響。ディレイは初回の時間設定と運転中スムージングを区別し、同期・ピンポン・バイパスを別に扱う。プリセット・手動編集とコントロールを同期。 | `SpaceEffectsTests` |
| FX-07 内蔵FX状態の復元 | [NativeEffects.cpp](../Source/NativeEffects.cpp)、[Equalizer.cpp](../Source/Equalizer.cpp)、[Engine.cpp](../Source/Engine.cpp) | 既定値 → プリセット／手動編集 → 保存用パラメーター → ロード時の検証・適用 → サンプルレートに合わせた再準備。DSP履歴は保存するパラメーターと独立。 | `EqualizerTests`、`NativeDynamicsTests`、`SpaceEffectsTests`、`ProjectSafetyTests` |

## 監査時に決めた改善の順序

1. **TR-03 録音確定の失敗を先に扱う。** `finishRecording()` は録音フラグを落としてからトラックを順に確定する。途中の `readAudio` が例外を投げたとき、前のテイクの追加は済み、後続の writer の後始末に達しない。先に全 writer を閉じ、トラック単位の成功／素材ファイルから復旧可能／失敗を返す設計にする。UXはどのテイクが救出できるか表示。DXは録音終了時の所有権を一箇所で管理。AXは部分成功を判定。**着手時のテスト**は二つの合成テイクのうち一方だけ読み込みに失敗させ、全 writer 解放、残った音声ファイル、追加済みテイク、次の録音への復帰を確認する。録音ファイルを削除する回復処理は追加しない。
2. **DOC-03 プロジェクト切り替えを準備と確定に分ける。** GUIがエディターを閉じてから `Engine::loadProject` が失敗し得る。デコード・プラグイン準備が成功した直後、旧プラグインの破棄より前という一点でウィンドウを閉じる設計が必要。単に `.clear()` を入れ替え後へ移すと旧インスタンスの寿命を壊す。UXは失敗後も編集位置と画面を保持。DXは準備失敗とコミットの境界を定義。AXは失敗と成功を区別して再試行。**着手時のテスト**は検証を通過した後に素材が読めないケースで、旧セッション・選択・エディターを確認する。
3. **APP-05／06 処理名と完了通知を統一する。** `runBackground` と録音確定が `status` に直接代入し、`setStatus` の保護期間を更新しない。過去の音声スキップ／ドロップのカウンターが残っていると、完了通知がタイマーで上書きされ得る。また終了を求めると音程処理中も「書き出し／スキャン中」と案内される。まず通知経路を揃え、次に実際の処理名と完了結果を持たせる。UXは結果が読める。DXは表示・ガード・処理名の二重管理を減らす。AXは日本語の表示文を解析せずに結果を判定できる。**着手時のテスト**は累積カウンターありで録音／書き出し完了を確認し、処理種別ごとの終了ガードも確認する。
4. **TR-02／04、TL-01、PITCH-02は、それぞれの状態を観測する窓口から始める。** 録音のフェーズや、範囲・ジェスチャーの遷移をメッセージスレッドで取得する読み取り専用の状態として定義する。同じ状態からボタン・ツールチップ・ガード・テストを導く。AXでは局所的な状態IDと失敗理由を使い、再描画の文言や時間待ちからの推測を減らす。状態IDやテスト用コマンドの追加は正規UI操作と同じ入口を使い、承認・録音ガードを通す。UIの状態が安定してから、複数フラグにまたがる排他的なジェスチャーだけを個別に整理する。

## 守るべき契約と検証範囲

- 巨大な状態enumを一つ作る案は採らない。再生、録音準備、プロジェクトの変更、エディターの試聴は独立または重なる。まず各所有者の契約をテストで固定する。
- UIの利便性の変更を音声コールバックのロック・割り当て・I/O増加で実現しない。サンプル境界、録音の生素材、VSTの寿命、旧プロジェクト形式を境界条件として守る。
- 既存の `--self-test <出力先>` と `tests/test_portable_install.py` が自動検証の入口。通常のUIへ新機能を追加する前に、開始、成功、取消、失敗、遅延コールバック、フォーカス喪失、対象の削除を該当する機械ごとに確認する。実機ASIO・任意のVST環境・全状態の組み合わせは今回の自動検証の範囲外。
- 配布側の状態は [portable_install.py](../tests/portable_install.py) が所有する。初回移行／安定先の更新／起動中で差し替え不能／再試行を分けており、既存Dataを保護する回帰テストがある。

次の変更では、特にこの受け渡しから回帰を確認する。

| 起点 → 受け手 | 試験で確認する条件 |
|---|---|
| 音声コールバック → GUIの録音確定 | 終端・デバイス中断・ユーザー停止が近接しても同じテイクを重複追加しない。壊れたテイクがあっても後続トラックの後始末が進む。 |
| ファイル選択 → 保存／承認 → 読み込み | 取消や失敗で元の画面と編集対象を保ち、承認は確認したスナップショットへ適用する。成功時のみ旧プロジェクトのウィンドウを閉じる。 |
| タイムラインの範囲 → ループ／録音／書き出し | メインの解除と音程エディターの解除を区別する。メインの解除後は青い選択を既定の書き出し範囲に使わない。録音中の取消ガードを通す。 |
| マウス・フォーカス → エディター → 音声試聴 | 押した音を離す、別のエディターに移る、対象を消す各経路で所有者に対応する試聴が止まる。ジェスチャー確定とNote Offを別に確認する。 |
| バックグラウンド完了 → 操作結果／画面再有効化 | 取消・対象の差し替え・遅延完了で古い結果が適用されない。UIでも自動テストでも、結果の成否と対象を識別できる形を目指す。 |

監査時の基準実行では 0.4.44 のネイティブセルフテスト **1,127項目**とポータブル更新の **5項目**が通過した。修正後に追加したテストは冒頭の対応表を参照。
