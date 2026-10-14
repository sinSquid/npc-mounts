unit NPCMountsCheck;
var report:TStringList; errors:Integer;
procedure CheckTree(e:IInterface);
var i:Integer;s:string;
begin
  s:=Check(e);
  if s<>'' then begin Inc(errors);report.Add('ERROR|'+Path(e)+'|'+s);end;
  for i:=0 to ElementCount(e)-1 do CheckTree(ElementByIndex(e,i));
end;
function Initialize:Integer;
var i,j:Integer;f:IInterface;
begin
  report:=TStringList.Create;errors:=0;
  for i:=0 to FileCount-1 do begin
    f:=FileByIndex(i);
    if not SameText(GetFileName(f),'NPCMounts.esp') then Continue;
    report.Add('FOUND|'+GetFileName(f));
    for j:=0 to ElementCount(f)-1 do CheckTree(ElementByIndex(f,j));
  end;
  report.Add('COMPLETE|errors='+IntToStr(errors));
  report.SaveToFile('E:\code-lib\me\npc-mounts\reports\xedit-check.txt');
  AddMessage(report.Text);report.Free;Result:=1;
end;
end.
