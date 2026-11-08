object Frm_main: TFrm_main
  Left = 222
  Top = 151
  BorderStyle = bsSingle
  Caption = #$009A#$0097#$0083#$0082#$008B#$0097#$0096#$0091
  ClientHeight = 240
  ClientWidth = 320
  Color = clBtnFace
  Font.Charset = SHIFTJIS_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'MS PGothic'
  Font.Style = []
  OldCreateOrder = False
  Position = poDesktopCenter
  OnDestroy = FormDestroy
  OnKeyDown = FormKeyDown
  PixelsPerInch = 96
  TextHeight = 12
  object DDDD1: TDDDD
    Left = 16
    Top = 20
    DebugOption = [ddoHaltOnError]
    InitialScreenWidth = 320
    InitialScreenHeight = 240
    BackColor = 0
    DisableScreenSaver = True
    Use3D = False
    D3DOptions = []
    VsyncAtWindowed = True
    OnInit = DDDD1Init
  end
  object Joy: TDDIDEX
    Left = 48
    Top = 20
    DebugOption = [dioHaltOnError]
  end
  object KbgmPlayer1: TKbgmPlayer
    Left = 112
    Top = 20
    AutoLoadMidis.Strings = (
      'midi\init'
      'midi\main'
      'midi\miss'
      'midi\gameover'
      'midi\rank'
      'midi\clear')
  end
  object DDSD1: TDDSD
    Left = 80
    Top = 20
    DebugOption = [dsoHaltOnError]
  end
end
