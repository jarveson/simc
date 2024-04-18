action_priority_list_t* precombat = get_action_priority_list( "precombat" );
action_priority_list_t* def = get_action_priority_list( "default" );
action_priority_list_t* aoe_builder = get_action_priority_list( "aoe_builder" );
action_priority_list_t* berserk = get_action_priority_list( "berserk" );
action_priority_list_t* builder = get_action_priority_list( "builder" );
action_priority_list_t* cooldown = get_action_priority_list( "cooldown" );
action_priority_list_t* finisher = get_action_priority_list( "finisher" );
action_priority_list_t* variables = get_action_priority_list( "variables" );

precombat->add_action( "cat_form,if=!buff.cat_form.up" );

def->add_action( "cat_form,if=!buff.cat_form.up" );
def->add_action( "call_action_list,name=variables" );
def->add_action( "call_action_list,name=cooldown" );

def->add_action( "auto_attack");
def->add_action( "tigers_fury,if=((set_bonus.tier13_4pc&energy<=45)|(!set_bonus.tier13_4pc&energy<=35))&(!buff.clearcasting.react)" );
def->add_action( "berserk,if=buff.tigers_fury.up|(target.time_to_die<buff.berserk.duration&cooldown.tigers_fury.remains>6)" );
def->add_action( "mangle_cat,if=set_bonus.tier11_4pc&buff.strength_of_the_panther.remains<4" );
def->add_action( "faerie_fire_feral,if=debuff.faerie_fire.stack<3&!(debuff.expose_armor.up)" );
def->add_action( "mangle_cat,if=debuff.mangle.remains<=2&(!debuff.mangle.up|debuff.mangle.remains>=0.0)" );
def->add_action( "ravage,if=(buff.stampede_cat.up|buff.smokescreen.up)&(buff.stampede_cat.remains<=1|buff.smokescreen.remains<=1)" );

def->add_action( "ferocious_bite,if=combo_points>=1&dot.rip.ticking&dot.rip.remains<=2.1&target.health.pct<=variable.bitewhp" );
def->add_action( "ferocious_bite,if=combo_points>=5&dot.rip.ticking&target.health.pct<=variable.bitewhp" );

def->add_action( "shred,extend_rip=1,if=position_back&dot.rip.ticking&dot.rip.remains<=4" );
def->add_action( "mangle_cat,extend_rip=1,if=position_front&dot.rip.ticking&dot.rip.remains<=4&target.health.pct>variable.bitewhp" );
def->add_action( "rip,if=combo_points>=5&target.time_to_die>=6&dot.rip.remains<2.0&(buff.berserk.up|dot.rip.remains<=cooldown.tigers_fury.remains)" );

def->add_action( "ferocious_bite,if=combo_points>=5&dot.rip.remains>5.0&buff.savage_roar.remains>=3.0&buff.berserk.up" );
def->add_action( "rake,if=target.time_to_die>=8.5&buff.tigers_fury.up&dot.rake.remains<9.0&(!dot.rake.ticking|dot.rake.multiplier<multiplier)" );
def->add_action( "rake,if=target.time_to_die>=dot.rake.remains&dot.rake.remains<3.0&(buff.berserk.up|energy>=71|(cooldown.tigers_fury.remains+0.8)>=dot.rake.remains)" );

def->add_action( "shred,if=position_back&buff.clearcasting.react" );
def->add_action( "mangle_cat,if=position_front&buff.clearcasting.react" );
def->add_action( "savage_roar,if=combo_points>=1&buff.savage_roar.remains<=1" );
def->add_action( "ravage,if=(buff.stampede_cat.up|buff.smokescreen.up)&cooldown.tigers_fury.remains=0" );
def->add_action( "ferocious_bite,if=(target.time_to_die<=4&combo_points>=5)|target.time_to_die<=1" );

if (true_level <= 80)
	def->add_action( "ferocious_bite,if=combo_points>=5&dot.rip.remains>=8.0&buff.savage_roar.remains>=4.0" );
else 
	def->add_action( "ferocious_bite,if=combo_points>=5&dot.rip.remains>=14.0&buff.savage_roar.remains>=10.0" );

def->add_action( "ravage,if=(buff.stampede_cat.up|buff.smokescreen.up)&!buff.clearcasting.react&buff.tigers_fury.up&energy.time_to_max>1.0" );
def->add_action( "mangle_cat,if=set_bonus.tier11_4pc&buff.strength_of_the_panther.stack<3" );

def->add_action( "shred,if=position_back&(buff.tigers_fury.up|buff.berserk.up)" );
def->add_action( "shred,if=position_back&((combo_points<5&dot.rip.remains<3.0)|(combo_points=0&buff.savage_roar.remains<2))" );
def->add_action( "shred,if=position_back&cooldown.tigers_fury.remains<=3.0" );
def->add_action( "shred,if=position_back&target.time_to_die<=8.5" );
def->add_action( "shred,if=position_back&energy.time_to_max<=1.0" );
def->add_action( "mangle_cat,if=position_front&(buff.tigers_fury.up|buff.berserk.up)" );
def->add_action( "mangle_cat,if=position_front&((combo_points<5&dot.rip.remains<3.0)|(combo_points=0&buff.savage_roar.remains<2))" );
def->add_action( "mangle_cat,if=position_front&cooldown.tigers_fury.remains<=3.0" );
def->add_action( "mangle_cat,if=position_front&target.time_to_die<=8.5" );
def->add_action( "mangle_cat,if=position_front&energy.time_to_max<=1.0" );

cooldown->add_action( "potion,if=buff.bloodlust.react|target.time_to_die<=40" );
cooldown->add_action( "use_items" );


variables->add_action( "variable,name=bitewhp,op=setif,value=60,value_else=25,condition=set_bonus.tier11_4pc" );