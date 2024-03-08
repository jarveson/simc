cd ..
set classes=warrior,hunter,monk,paladin,rogue,shaman,mage,warlock,druid,deathknight,priest,demonhunter,evoker

for %%i in (%classes%) do (
E:\gitrepos\simc\out\build\x64-Debug\simc display_build="0" spell_query="spell.class=%%i">spelldatadump/%%i.txt
)

E:\gitrepos\simc\out\build\x64-Debug\simc display_build="0" spell_query="spell">spelldatadump/allspells.txt

E:\gitrepos\simc\out\build\x64-Debug\simc display_build="2">spelldatadump/build_info.txt
