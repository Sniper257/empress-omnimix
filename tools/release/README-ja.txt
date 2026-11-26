EMPRESS MEGAMIX - パブリックベータ
=================================

本作は非公式のファンプロジェクトです。KONAMIおよびBEMANIとは関係がなく、
公認や支援を受けているものではありません。商標は各権利者に帰属します。
MEGAMIXは無料です。

MEGAMIXを使う前に、元のセーブを必ずバックアップしてください。
不具合によってセーブが破損する可能性が、わずかながら残っています。
自作ソフトのメモリーカード用ファイルマネージャーでセーブを取り出し、
バックアップは別のデバイスに保管してください。PCSX2では仮想メモリーカードの
ファイルをバックアップしてください。

既知の不具合・制限
------------------

- メモリ不足により、一部の曲でロード中にフリーズすることがあります。
- Floating Hi-SpeedとLIFTの併用には不具合があります。
  緑数字（GN）の調整がうまくいかない場合はLock CoverをOFFにしてください。
- LIFTの画像はレーンカバーと共通です。
- ELFをMEGAMIXフォルダの外に置くと、ランチャーの設定が正しく保存されない
  場合があります。
- STATISTICSとRIVALは手を入れていないため、正常に動かない可能性があります。
- MEGAMIXの全解禁はGALLERYには適用されません。
- BEGINNERメニューには追加曲は表示されません。

不具合の報告
------------

以下の情報を添えて、GitHubのIssuesに報告してください。
https://github.com/marysiamzawka/empress-megamix/issues

- 本体の型番（例：SCPH-50004）。
- 使用ストレージ（USBまたはMMCE）。
- MEGAMIXに追加した作品。
- 接続していたコントローラー。
- 使用中のlauncher.cfg。コントローラー関連の不具合なら、controllers.0と
  controllers.1も添付してください。
- メモリーカード内のセーブフォルダBISLPM-55221BM2DX16。
  元のEMPRESSのセーブファイルだけでなく、フォルダ全体を送ってください。
- 発生した症状と再現手順。必要に応じて曲名、難易度、プレイモードも記載してください。

設定ファイルは通常MEGAMIXフォルダ内にあります。ELFを別の場所から起動している場合は、
ランチャーで選んだ設定の保存先を確認してください。MMCEでは、MEGAMIXが使用している
仮想メモリーカードからセーブを取り出してください。

準備
----

必要なもの：

- Python 3.10以上。
- 自分で所有しているEMPRESS + PREMIUM BESTのDisc 1（EMPRESS）のISOバックアップ。
  他の対応PS2作品も、自分で吸い出したISOから曲を追加できます。
- PS2で自作ソフトを起動できる環境、またはPCSX2。
  起動方法が必要ならFMCB、PS2BBL、FreeDVDBootなどを調べてください。
- 5 GB以上の空き容量があるUSBストレージまたはMMCEデバイス（PSxMemCard Gen2など）。
  基本セットは約4.3 GiB、全対応ディスクを入れた構成は約36.2 GiBです。

対応するディスクイメージのRedump SHA-1ハッシュは以下のとおりです。

    3rd style      495f5f9fc6b62562c92b27be31c7d1a9bae64b31
    4th style      69c73667a7b2fc6e6fd2314899377f62cf16d534
    5th style      abda772a65b7d4ed0d7175358af9ca9209c03d37
    6th style      0010d44b65c5eeb9360c9eace11f1ba840c01c58
    7th style      090ca87e447106368c0c89f63e68a4b6c318b5d9
    8th style      01d2a7ffa427d73996b4a19fab14d1c351012377
    9th style      6d42643fb992c19737b22813166a12b5d60bd69e
    10th style     680edcac40ed72ed318bc1bb8d6e89224b80a330
    IIDX RED       8e52f963a33c9fe95022861ef39966bde715ed1a
    HAPPY SKY      b82e7939b720b8a1c0875e88a328493f336a758f
    DistorteD      706da91072b75262729c96b16aa91181231184d2
    GOLD           7cc3625f57f4d77d67ac3fb3675d3ee5de18e726
    DJ TROOPERS    6a35e096900dc6e7842f7aa86366ba0f5fe78dfd
    EMPRESS        92d95574a6a35a908cc3255a997a15f126993ada
    PREMIUM BEST   b23f9799b2cb6106b45c992928c97d6f2e4d3a8b
    beatmania USA  497cd0a20420d270047a77009efc7c48ce95f727

基本セットの作成
----------------

ビルダーのZIPを展開し、中のファイルは同じフォルダに置いたままにしてください。
そのフォルダでターミナルを開き、以下を実行します。

    python3 megamix.py "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX"

Windowsではpython3の代わりにpyを使います。

    py megamix.py "D:\ISOs\EMPRESS DVD1.iso" "D:\MEGAMIX"

出力先には新しいフォルダを指定してください。
標準フォントを使う場合はPythonだけで生成でき、SDKやコンパイラーは不要です。
カスタムフォントを使う場合は、基本セットを作る前に以下の設定を行ってください。

カスタムフォント（任意）
------------------------

基本セットを作る前に、.ttf / .otf / .ttc / .otcを対応するフォルダに入れてください。

    fonts/compacta/   大きな曲名と曲名カード（Compacta BT）
    fonts/eurostile/  選曲バーの曲名とカードのジャンル
    fonts/handel/     Eurostileより優先
    fonts/helvetica/  ジャンルとアーティスト名
    fonts/gothic/     日本語ゴシック体（DF HeiSei Gothic）
    fonts/mincho/     曲名とカードのアーティスト名用の日本語明朝体（DF HeiSei Mincho）

ファイル名は自由です。OSへのフォント登録やJSON編集は不要です。
不足する書体・文字は同梱の代替フォントで補います。別のフォントを各枠に入れても構いません。
書体や太さはメタデータから選ばれ、使用するものが表示されます。

基本セットを作るPCに追加パッケージを入れてください。Windowsの場合：

    py -m pip install Pillow fonttools

Linux/macOSでシステム管理のPythonを使う場合は、仮想環境を作成します。

    python3 -m venv .venv
    .venv/bin/python -m pip install Pillow fonttools

この環境を使う間は、以降のpython3を.venv/bin/pythonに読み替えてください。
通常どおり基本セットを作ればfontsを自動検出します。別の場所を使う場合は
--fonts "path/to/fonts"、カスタムフォントを使わない場合は--no-custom-fontsを追加します。

追加パックは同じビルダーで通常どおり生成・インストールしてください。
基本セットに保存した描画用データを使うため、追加時は元のフォントや追加パッケージは不要です。
追加パックの生成は基本セットより先でも構いません。追加側の--fontsは不要です。
指定した場合は、同じフォントプロファイルの基本セットが必要になります。

基本セットをコピーするときは.megamix/fontsも残してください。
元のフォントファイルはゲームや追加パックに入りません。変わるのは生成テキストだけで、
元の画像、ランチャーUI、起動画面の文字は変わりません。フォントを変える場合は
基本セットを新規作成し、追加パックを入れ直してください。画像だけの手動上書きは避けてください。

既存のインストールを更新
------------------------

データ契約が同じ新しいリリースなら、作成済みの基本セットや追加パックを作り直さずに、
ランチャー、ランタイム、MODだけを入れ替えられます:

    python3 megamix.py update "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX"

基本セットの作成に使ったISOを指定してください（ランチャー用のデータを2つ読むだけです）。
追加パック、設定、セーブ、曲フォルダーはそのまま残ります。新しい基本セットが必要な
リリースの場合はその旨を表示し、何も変更しません。ランチャーキャッシュを使っている
場合は更新後に作り直してください。

他のディスクから曲を追加
------------------------

追加パックを作成し、基本セットにインストールします。

    python3 megamix.py "path/to/HAPPY SKY.iso" "path/to/HAPPY-SKY-pack"
    python3 megamix.py install "path/to/HAPPY-SKY-pack" "path/to/MEGAMIX"

追加したいディスクごとに繰り返してください。順番は問いません。
パックの中身を手動でコピーせず、installコマンドを使ってください。

完成したMEGAMIXフォルダをUSBストレージまたはMMCEのカードへコピーし、
安全な取り外しを行ってください。ISOやビルダーのファイルはPC側に置いておきます。

エミュレーター / HostFs用
-------------------------

PCSX2向けには、HostFs用の基本セットを別に作成します。

    python3 megamix.py "path/to/EMPRESS DVD1.iso" "path/to/MEGAMIX-host" --target host

PCSX2でHostFsを有効にし、出力されたMEGAMIX.ELFを起動してください。
曲の追加には同じ追加パックとinstallコマンドを使えます。
実機用とHostFs用の基本セットは分けてください。

検証と中断後の再開
------------------

インストール先のファイルを検証するには、以下を実行します。

    python3 megamix.py verify "path/to/MEGAMIX"

作成やインストールが中断された場合は、同じコマンドを再実行すると再開できます。
作業途中のフォルダと復旧用ジャーナルは残しておいてください。Ctrl+Cで正常終了します。
強制終了後にロックファイルが残った場合は、元のプロセスが停止していることを確認し、
エラーメッセージに示されたロックファイルだけを削除してください。

PCでランチャーのキャッシュを作成（任意）
----------------------------------------

追加するディスクの数によっては、実機の初回起動にかなり時間がかかります。

PCでキャッシュを作成しておくと、この待ち時間を短縮できます。

コマンドを実行するPython環境にWasmtimeを入れます。

    python3 -m pip install "wasmtime>=50.0.1,<51"

Windowsはpython3をpyに読み替えます。システム管理のPythonでは上記の仮想環境を使ってください。
PC上のフォルダと、PS2で使う正確なゲームデータのパスを指定します。

    python3 megamix.py cache "X:\MEGAMIX" --game-path mmce0:/MEGAMIX/
    python3 megamix.py cache "/media/USB/Games/BM" --game-path mass:/Games/BM/

--game-pathはELFの場所ではなく、PS2上のゲームデータのパスです。
MMCEはスロット1がmmce0:、スロット2がmmce1:、USBはmass:です。
mass:とmass0:の違いも含め、ランチャーのパスと完全に一致させてください。
違う場合はキャッシュが再生成されます。例：mmce1:/MEGAMIX/、mass:/Games/BM/。
HOSTはhost:です。

PS2で使うlauncher.cfgを指定してください。設定が別の場所にある場合：

    python3 megamix.py cache "X:\MEGAMIX" --game-path mmce1:/MEGAMIX/ --config-dir "Y:\APPS\MEGAMIX" --settings-path mc0:/APPS/MEGAMIX/

--config-dirはPCの設定フォルダ、--settings-pathはそのPS2上のパスです。
debug.cfgはdebug 0とprepprobe 0にしてください。設定保存や追加・更新が未完了なら先に
完了させてください。インストーラーのロック中や基本ファイルの破損時は生成できません。

起動
----

普段使っている自作ソフト用ランチャーからMEGAMIX.ELFを起動してください。
ELFはゲームのフォルダの外に置いても構いません。
ゲームデータが見つからない場合は、ストレージブラウザーでMEGAMIXフォルダを選んでください。

ランチャーの操作
----------------

- DualShock：方向キーで移動・値の変更、×で決定。
- beatmaniaコントローラー：1鍵で下、2鍵で上、6鍵で決定。
- SELECTでPS2接続のbeatmaniaコントローラー用操作をON/OFF。
- STARTでゲームを起動。サブメニューにはExitもあります。

PS2接続のコントローラーと設定済みのUSBコントローラーは、どちらもランチャーの操作に使えます。

メインメニューの項目：

- Video：480i、480p・448ライン、480p・480ライン全表示。
  ランチャーも選択したモードで表示されます。CRTで480pを使うには、
  水平同期31 kHz対応のディスプレイとコンポーネントケーブル、
  またはSync-on-Greenに対応したRGB接続が必要です。
- Songs：インストール済みの曲フォルダを有効・無効にします。
- Skin settings：レーンの明るさ、キービームの長さ、ノーツ画像、
  ノーツの高さ、フレームパックの初期設定。詳細はゲーム内のスキンエディターで変更できます。
- Autosave：従来の終了時セーブ、または選曲画面に戻った後のセーブを選択します。
  ゲーム内のAUTOSAVEもONにする必要があります。
- Bind USB controller：各プレイヤーのUSBコントローラーを割り当て・設定します。
- Change game file location：別のインストール先を選びます。
  変更後はランチャーを再起動してください。

初回起動はキャッシュを生成するため時間がかかります。
次回以降はキャッシュを再利用します。ディスクの追加や有効なフォルダの変更で、
再生成が必要になる場合があります。

USBコントローラー
-----------------

プレイヤーを選び、DualShock・7-key・5-keyのいずれかを選択してから操作を割り当てます。
ターンテーブルには、ボタン、スティック型の軸、回転式の絶対値軸を使えます。
各割り当ての間でボタンなどを離し、保存前に未設定・重複の警告を確認してください。

一般的なPC用beatmaniaコントローラーを含む、標準USB HIDコントローラーに対応しています。
独自プロトコルの機器や、一部のデバイスは動作しない場合があります。
同じ機種を2台使う場合も、それぞれ別のプレイヤーに割り当てられます。

ゲーム内では、各プレイヤーはPS2接続かUSBのどちらか一方を使います。
ゲーム中にUSBコントローラーを再設定・交換したい場合は、ランチャーでUSBを割り当ててください。
ゲーム起動後にPS2接続とUSBを切り替えることはできません。

ゲーム内
--------

基本的な流れはEMPRESSと同じです。追加機能の多くはFREEモード向けです。
曲と従来のCUSTOMIZEアイテムは最初から解禁されています。

MOD MENU
--------

選曲画面で以下を同時押しすると開きます。

- SP / 2人BATTLE：自分のコントローラーの1 + 3 + 5鍵。
- DP：左右両側の1 + 3鍵。
- DualShock：□ + × + ○。DPでは両方のコントローラーで□ + ×。
  標準のボタン配置を前提としています。

一度ボタンをすべて離してから、コマンドを入力してください。

メニュー操作：

- DualShock：方向キー、○で決定、□で戻る。
- 7-key：1鍵で下、2鍵で上、6鍵で決定。
- 5-key：1鍵で下、2鍵で上、5鍵で決定。

項目を決定して編集し、もう一度決定すると編集を終了します。
STARTまたはSAVE / EXITで保存して閉じ、SELECTまたはEXITで変更を破棄して閉じます。
コントローラーの割り当て中は、その画面の案内に従ってください。
STARTとSELECT自体を割り当てる場合もあります。
完了済みのコントローラー設定の保存は、後で他のメニュー変更を破棄しても残ります。

Lane options（譜面オプション）
------------------------------

- NORMAL、MIRROR、RANDOM、S-RANDOM、H-RAN、ALL-SCR。
- R-RANDOM、Random Trainer。
- RANDOM+、S-RAN+、MIRROR+：先に対応する基本オプションを選んでください。
- Trainer pattern：7鍵の配置を直接指定するか、白鍵・黒鍵（青ノーツ）の配置先を指定し、
  各色のグループ内でランダムに入れ替えます。
- Field：DPでは左側・右側のTrainer配置を選びます。

R-RANDOMとTrainerは7鍵プレイ用です。
このビルドでは、Trainerを使用しても通常のスコア・クリアランプを保存できます。

Player options（プレイヤー設定）
--------------------------------

- Gauge Auto Shift：HARDゲージが尽きても、より易しいゲージでプレイを続けられます。
  RESULTでSELECTを押すと、表示するゲージの結果を切り替えられます。
- Floating Hi-Speed：FHSを有効にします。Lock cover / START resetは別途設定できます。
  操作は「プレイ」を参照してください。
- LIFT：下側のカバーと判定ラインの引き上げを有効にします。

Adjustment timing（判定タイミング調整）
--------------------------------------

メインのOptions画面に戻らずに判定オフセットを変更できます。
BATTLEではプレイヤーごとに設定でき、DPでは左右共通の値を使います。

Game options（ゲーム設定）
--------------------------

- REGUL SPEED：BPMに関係なくスクロール速度を一定にします。いわゆるCModです。
- GAMBOL TIMING：OFF、HYPER、ANOTHER。
- HAZARD：1ミスで終了。HARDが必要です。
- QUICK RETRY：「プレイ」にあるリトライ操作を有効にします。

ピンク色の項目は、スコア保存の制限やアシストクリア扱いになる設定を示します。
制限が有効な間は、メニュー外にもASSIST ONが表示されます。
CMD:には有効な譜面オプションやTrainer配置が表示されます。
一部の従来の隠しコマンド系オプションは、プレイヤー間で共通です。

Rebind controller（コントローラー再設定）
----------------------------------------

USBを割り当て済みのプレイヤー向けです。

- Rebind buttons：現在の割り当てを編集します。
- Assign new controller：元のコントローラーを抜いて新しいものを接続し、
  種類の選択、ボタン割り当て、ターンテーブル設定を行います。
- Change controller type：割り当てを消さずに種類を変更します。

Customize play scene（プレイ画面のカスタマイズ）
------------------------------------------------

ライブスキンエディターを開きます。FAST/SLOWとBPM変化通知もここで設定できます。

スキンカスタマイズ
------------------

選択中のレイアウトでAUTOのプレビュー譜面を読み込みます。
プレイ画面を見ながら設定を変更し、保存またはキャンセルしてください。

設定できる項目：

- Lane light：レーンの明るさ。
- Keybeam height：キービームの高さ。
- Note height：ノーツの高さ。
- Judgement height：判定文字の高さ。
- FAST/SLOW：OFF、TYPE A / B / C。
- F/S counters：判定数表示の横にFAST/SLOWの累計を表示・非表示。
- BPM notice：OFF、TYPE A / B / C / D。
- Ghost：OFF、TYPE A / B / C。
- Frame：従来のEMPRESSのフレーム。
- Notes：EMPRESSのノーツ画像。
- Explosion art：RANDOMを含む、従来のEMPRESSの判定爆発。
- Explosion size：70～130%、10%刻み。標準は100%。
- Turntable：ターンテーブル。
- Lane cover：レーンカバー。
- Play layout：SP・DPのOriginal / Arcade。
- DP type：Type A / B。

BATTLEではP1とP2の値が横に並びます。対応する設定・画像はプレイヤーごとに変更でき、
フレームとレイアウトは共通です。DPは1人分の設定を左右両方に使います。

対応する画像の変更では、プレビューを止めずに切り替えます。
レイアウトの変更時は再読み込みします。OriginalのDPでは、BGA部分の代わりに
画面中央のオーバーレイを使います。

プレイ
------

Floating Hi-Speed（FHS）
-----------------------

Player optionsでFHSを有効にした後、STARTを押しながらSELECTを短く押して離すと、
通常ハイスピードとフローティングハイスピードを切り替えられます。

Lock cover / START resetがONの場合、READYが出るとカバー位置が固定されます。
プレイ中のSTART 2連打はSUDDEN+の切り替えではなく、現在のBPMで目標の緑数字に
合わせ直す操作になります。READY前は従来どおりカバーを操作できます。
START + SELECTの長押しによる終了も、そのまま使えます。

LIFT
----

STARTを押しながらターンテーブルを回すとカバーを調整できます。
SUDDEN+が表示されている間はSUDDEN+を調整します。STARTを2連打してSUDDEN+を消し、
LIFTを調整してから、もう一度2連打でSUDDEN+を戻してください。
FHSのカバーロックを使う場合は、両方ともREADY前に調整してください。

リトライとRANDOM配置の維持
--------------------------

Quick RetryがONのとき、終了時のフェード中またはSTAGE FAILEDの演出中に押します。

- START：新しいRANDOM配置でリトライ。
- SELECT：前回の配置を維持してリトライ。

RESULTでCONTINUEを選ぶ際にSELECTを押したままにし、画面が切り替わるまで押し続けると、
同じ配置を維持できます。SELECTを押さずにCONTINUEすると、新しい配置になります。

セーブ
------

MEGAMIXは、従来の曲数上限に縛られない省容量のセーブ形式を使います。
スコアはスロット1のメモリーカードに保存します。実物のメモリーカードでも、
MMCEのメモリーカード機能でも構いません。ELFやゲームデータの場所を変えても、
スコアの保存先は変わりません。

MEGAMIXのセーブがなく、通常のEMPRESSのセーブがある場合は、それを読み込みます。
保存するとMEGAMIX用のデータが追加されます。元のセーブはそのままEMPRESSで使えますが、
その後MEGAMIXで更新した記録は反映されません。

対応するセーブがない場合は、ゲーム内で新規データを作り、手動でSAVEしてください。
PC側のビルダーはスコアのセーブを作りません。ランチャーでプレイ後のセーブを選んだ場合も、
ゲーム内のAUTOSAVEをONにする必要があります。

新形式は2世代を保持するため、保存が中断されても前の世代に戻せます。
ただし正常なカードは必要です。保存中に電源を切ったり、カードを抜いたりしないでください。
FAST/SLOWの累計は表示のみで、セーブされません。

ランチャーとMOD MENUの設定は、スコアのセーブとは別です。
Game ID対応のMMCEデバイスでは、標準でPIZD-00000を使います。
通常版のセーブを引き継ぐ場合は、その仮想カードに入れてください。
他のVMCを自動で探すことはありません。

謝辞
----

以下のプロジェクトに携わる皆さんに感謝します。

- bluric氏の480pパッチ（https://github.com/blurtic2/iidx-ps2-480p）：
  このプロジェクトのきっかけ。
- 95jack氏のEMPRESS CS FAST/SLOWパッチ（https://www.youtube.com/watch?v=anNfo8uBubE）。
- PS2SDK / ps2dev（https://github.com/ps2dev/ps2sdk）：SDKとドライバー。
- gsKit（https://github.com/ps2dev/gsKit）：ランチャーの描画。
- MMCEMAN（https://github.com/ps2-mmce/mmceman）：MMCE対応。
- FatFs、ChaN氏（https://elm-chan.org/fsw/ff/）：ファイルシステム。
- ps2-packer（https://github.com/ps2dev/ps2-packer）：ELF圧縮。
- 2dx-gsm、aixxe氏（https://github.com/aixxe/2dx-gsm）：Gauge Auto Shiftの参考。
- iidx-ps2tools、SaxxonPike氏（https://github.com/SaxxonPike/iidx-ps2tools）：
  PS2のデータ形式と変換ツール。
- iidx2bms、Glebsin氏（https://github.com/Glebsin/iidx2bms）：譜面形式の参考。
- xdelta（https://github.com/jmacd/xdelta）：バイナリ差分レシピ。
- Python（https://www.python.org/）、Pillow（https://python-pillow.org/）、
  NumPy（https://numpy.org/）：開発・アセット作成用ツール。
  ビルダーの実行に必要なのはPythonだけです。

調査とテストでは、以下も使用しました。
PCSX2（https://github.com/PCSX2/pcsx2）、
Ghidra（https://github.com/NationalSecurityAgency/ghidra）、
Open PS2 Loader（https://github.com/ps2homebrew/Open-PS2-Loader）、
OpenLR2（https://github.com/GOMazk/OpenLR2）、
beatoraja（https://github.com/exch-bms2/beatoraja）、
lr2oraja-endlessdream（https://github.com/seraxis/lr2oraja-endlessdream）。

ここに挙げたすべてのプロジェクトが同梱されているわけではありません。
同梱物のライセンス表記・クレジットはlicensesフォルダを確認してください。
