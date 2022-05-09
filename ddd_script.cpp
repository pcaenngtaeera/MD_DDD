#include <Windows.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <thread>

#include "const.h"
#include "duel.h"
#include "dynamite.h"
#include "input.h"
#include "engine.h"
#include "nav.h"

std::atomic<bool> DUEL_END;
std::thread GAME_MONITOR;

int SELF;
int RIVAL;
int UPSTART_LP_CEILING;

bool REMOVE_BANQUET_FROM_TRICK = false;

void monitor(std::atomic<bool>& DUEL_END)
{
	while (!DUEL_END)
	{
		if (IsDuelEnd()) DUEL_END = true;		
		if (get_card_at(RIVAL, Position::Graveyard, 0) == CardID::Dimension_Shifter) DUEL_END = true;
		Sleep(1000);
	}
}

enum State
{
	SET_TRAP,
	SUMMON_MONSTER,
	USE_SPELL,
	USE_DEMISE,
	USE_SET_SPELL,
	WIN
};

std::set<int> STZ_REMOVAL =
{
	CardID::Mystical_Space_Typhoon,
	CardID::Twin_Twisters,
	CardID::Cosmic_Cyclone,
	CardID::Red_Reboot,
};

Dynamite::Dynamite() :
	STATE(State::SET_TRAP),
	TOON_TABLE_IN_DECK(3),
	TOON_WORLD_IN_DECK(true),
	TOON_WORLD_REMOVED(false),
	DYNAMITE_COUNT(0),
	NORMAL_SUMMONED(false),
	SET_BANQUET(false),
	SET_TRICK(false),
	USED_LAMENT(false),
	USED_TRICK(false),
	SET_DEMISE(false),
	USED_DEMISE(false),
	USED_THOUSAND(false),
	USED_DUALITY(false),
	USED_MOON(false),
	USED_POPPERUP(false)
{
	std::cout << "──────────────────────────" << std::endl;
	std::cout << "┌───────────┐" << std::endl;
	std::cout << "│   BEGIN   │" << std::endl;
	std::cout << "└───────────┘" << std::endl << std::endl;

	GAME_MONITOR = std::thread(monitor, std::ref(DUEL_END));

	DUEL_END = false;
	run();
	if (DUEL_END) surrender();

	GAME_MONITOR.join();
	
	std::cout << std::endl;
	std::cout << "┌───────────┐" << std::endl;
	std::cout << "│    END    │" << std::endl;
	std::cout << "└───────────┘" << std::endl;
}

void Dynamite::run()
{
	while (GetCurrentPhase() != Phase::Main1)
	{
		if (DUEL_END) return;
		send_right_click();
		Sleep(1000);
	}

	SELF = IsRival(0);
	RIVAL = IsMyself(0);
	UPSTART_LP_CEILING = ((card_top_of(RIVAL, Position::Extra_Deck) + 1) * 300 * 2) - 1000;

	if (card_top_of(RIVAL, Position::Extra_Deck) < 13)
	{
		if (card_top_of(RIVAL, Position::Extra_Deck) < 11)
		{
			std::cout << "::: SURRENDER (ED - " << card_top_of(RIVAL, Position::Extra_Deck) << ")" << std::endl;
			DUEL_END = true;
			return;
		}

		bool THOUSAND_IN_HAND = false;

		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (get_card_at(SELF, Position::Hand, i) == CardID::Contract_with_Don_Thousand)
			{
				THOUSAND_IN_HAND = true;
				break;
			}
		}

		if (!THOUSAND_IN_HAND)
		{
			std::cout << "::: SURRENDER (ED - " << card_top_of(RIVAL, Position::Extra_Deck) << ")" << std::endl;
			DUEL_END = true;
			return;
		}
		else
		{
			FORCED_THOUSAND = true;
			STATE = State::SUMMON_MONSTER;
		}
	}

	if (turn_number() == 0 && current_turn() != SELF)
	{
		std::cout << "::: SURRENDER (SECOND)" << std::endl;
		DUEL_END = true;
		return;
	}

	for (int i = 0; i < hand_count(SELF); i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Toon_Table_of_Contents) TOON_TABLE_IN_DECK--;
		else if (get_card_at(SELF, Position::Hand, i) == Toon_World) TOON_WORLD_IN_DECK = false;
	}

	if ((TOON_TABLE_IN_DECK == 0) || (TOON_TABLE_IN_DECK == 1 && !TOON_WORLD_IN_DECK))
	{
		std::cout << "::: SURRENDER (TOONS)" << std::endl;
		DUEL_END = true;
		return;
	}

	while (!DUEL_END)
	{

		while (an_action() == 0)
		{
			if (DUEL_END) return;
			send_right_click();
			Sleep(1000);
		}
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (TOON_WORLD_IN_DECK && get_card_at(SELF, Position::Hand, i) == CardID::Toon_World) TOON_WORLD_IN_DECK = false;
		}
		if (STATE == State::WIN) win();
		else if (can_win())
		{
			if (NORMAL_SUMMONED && !USED_LAMENT) use_lament();
			if (is_win())
			{
				STATE = State::WIN;
			}
		}
		else if (STATE == State::SET_TRAP)
		{
			if (free_stz(SELF)) set_trap();
			else DUEL_END = true;
			// scan for useable set spells and use them...
		}
		else if (STATE == State::SUMMON_MONSTER) summon_monster();
		else if (STATE == State::USE_SPELL)
		{
			if (free_stz(SELF)) use_spell();
			else DUEL_END = true;
			// scan for useable set spells and use them...
		}
		else if (STATE == State::USE_DEMISE) use_demise();
		else if (STATE == State::USE_SET_SPELL) use_set_spell();
	}
}

void Dynamite::set_trap()
{

	int CARDS_IN_HAND = hand_count(SELF);

	if (CARDS_IN_HAND == 0)
	{
		if (SET_DEMISE && !USED_DEMISE) STATE = State::USE_DEMISE;
		else DUEL_END = true;
		return;
	}

	if (SET_TRICK)
	{
		int DYNAMITES_IN_DECK = 3 - DYNAMITE_COUNT;
		for (int i = 0; i < CARDS_IN_HAND; i++) if (get_card_at(SELF, Position::Hand, i) == CardID::D_D_Dynamite) DYNAMITES_IN_DECK--;
		if (DYNAMITES_IN_DECK < 2)
		{
			std::cout << "::: TRICK (DISABLED)" << std::endl;
			SET_TRICK = false;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::D_D_Dynamite && DYNAMITE_COUNT != 2)
		{
			if (DYNAMITE_COUNT == 1 && SET_TRICK)
			{
				std::cout << "::: TRICK (DISABLED)" << std::endl;
				SET_TRICK = false;
			}
			std::cout << "::: DYNAMITE" << std::endl;
			SetSTZ(SELF, i);
			while (an_action() == 0)
			{
				if (DUEL_END) return;
				Sleep(1000);
			}
			DYNAMITE_COUNT++;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Banquet_of_Millions && !SET_BANQUET)
		{
			std::cout << "::: BANQUET" << std::endl;
			SetSTZ(SELF, i);
			while (an_action() == 0)
			{
				if (DUEL_END) return;
				Sleep(1000);
			}
			SET_BANQUET = true;

			if (NORMAL_SUMMONED && !USED_LAMENT)
			{
				use_lament();
			}

			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Trap_Trick && !SET_TRICK && DYNAMITE_COUNT < 2)
		{
			std::cout << "::: TRICK" << std::endl;
			SetSTZ(SELF, i);
			while (an_action() == 0)
			{
				if (DUEL_END) return;
				Sleep(1000);
			}
			SET_TRICK = true;
			return;
		}
	}

	if (!NORMAL_SUMMONED) STATE = State::SUMMON_MONSTER;
	else STATE = State::USE_SPELL;
}

void Dynamite::summon_monster()
{
	for (int i = 0; i < hand_count(SELF); i++)
	{
		if (get_card_at(SELF,Position::Hand,i) == CardID::Lilith_Lady_of_Lament)
		{
			std::cout << "::: LAMENT (SUMMON)" << std::endl;
			summon_lament(i);

			if (NORMAL_SUMMONED && !USED_LAMENT && SET_BANQUET && !can_win())
			{
				use_lament();
			}

			break;
		}
	}

	STATE = State::USE_SPELL;
}

void Dynamite::use_spell()
{
	int CARDS_IN_HAND = hand_count(SELF);

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if ((get_card_at(SELF, Position::Hand, i) == CardID::Toon_Table_of_Contents) && ((TOON_TABLE_IN_DECK + TOON_WORLD_IN_DECK) > 0) && (hand_count(SELF) > 1 || (SET_DEMISE && !USED_DEMISE)))
		{
			if (TOON_WORLD_REMOVED && TOON_TABLE_IN_DECK == 0) return;
			std::cout << "::: TOON" << std::endl;
			use_toon(i);
			STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Contract_with_Don_Thousand && FORCED_THOUSAND)
		{
			std::cout << "::: THOUSAND (FORCED)" << std::endl;
			if (use_thousand(i)) { STATE = State::SET_TRAP; FORCED_THOUSAND = false; }
			else DUEL_END = true;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Into_the_Void && hand_count(SELF) >= 3)
		{
			std::cout << "::: VOID" << std::endl;
			if (use_void(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			for (int i = 0; i < hand_count(SELF); i++)
			{
				if (get_card_at(SELF, Position::Hand, i) == CardID::Into_the_Void)
				{
					STATE = State::USE_SPELL;
					break;
				}
			}
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Upstart_Goblin && GetLP(RIVAL) <= UPSTART_LP_CEILING)
		{
			std::cout << "::: UPSTART" << std::endl;
			if (use_upstart(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Card_of_Demise && !SET_DEMISE)
		{
			std::cout << "::: DEMISE (SET)" << std::endl;
			SetSTZ(SELF, i);
			while (an_action() == 0)
			{
				if (DUEL_END) return;
				Sleep(1000);
			}
			Sleep(2000);
			SET_DEMISE = true;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Contract_with_Don_Thousand && !USED_THOUSAND && free_stz(SELF) > 1)
		{
			std::cout << "::: THOUSAND" << std::endl;
			if (use_thousand(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Pot_of_Duality && !USED_DUALITY)
		{
			std::cout << "::: DUALITY" << std::endl;
			if (use_duality(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Performapal_Popperup && !USED_POPPERUP && hand_count(SELF) >= 4)
		{
			std::cout << "::: POPPERUP" << std::endl;
			if (use_popperup(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Gold_Moon_Coin && !USED_MOON && hand_count(SELF) >= 3)
		{
			std::cout << "::: MOON" << std::endl;
			if (use_moon(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Performapal_Popperup && !USED_POPPERUP && hand_count(SELF) >= 2)
		{
			std::cout << "::: POPPERUP" << std::endl;
			if (use_popperup(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Reload && hand_count(SELF) >= 2)
		{
			std::cout << "::: RELOAD" << std::endl;
			if (use_reload(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < CARDS_IN_HAND; i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Magical_Mallet && hand_count(SELF) >= 2)
		{
			std::cout << "::: MALLET" << std::endl;
			if (use_mallet(i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	if (SET_DEMISE && !USED_DEMISE) STATE = State::USE_DEMISE;
	else if (USED_DEMISE && !USED_SET_SPELL)
	{
		STATE = State::USE_SET_SPELL;
		return;
	}
	else
	{
		std::cout << "::: 0 SPELLS" << std::endl;
		DUEL_END = true;
	}	
}

void Dynamite::use_set_spell()
{
	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Upstart_Goblin && GetLP(RIVAL) <= UPSTART_LP_CEILING)
		{
			std::cout << "::: UPSTART (STZ)" << std::endl;
			if (use_upstart(0, Position::STZ_0 + i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Performapal_Popperup && !USED_POPPERUP && hand_count(SELF) >= 3)
		{
			std::cout << "::: POPPERUP (STZ)" << std::endl;
			if (use_popperup(0, Position::STZ_0 + i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Gold_Moon_Coin && !USED_MOON && hand_count(SELF) >= 2)
		{
			std::cout << "::: MOON (STZ)" << std::endl;
			if (use_moon(0, Position::STZ_0 + i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Performapal_Popperup && !USED_POPPERUP && hand_count(SELF) >= 1)
		{
			std::cout << "::: POPPERUP (STZ)" << std::endl;
			if (use_popperup(0, Position::STZ_0 + i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Magical_Mallet && hand_count(SELF) >= 1)
		{
			std::cout << "::: MALLET (STZ)" << std::endl;
			if (use_mallet(0, Position::STZ_0 + i)) STATE = State::SET_TRAP;
			else STATE = State::USE_SPELL;
			return;
		}
	}

	USED_SET_SPELL = true;
	STATE = State::USE_SPELL;
}

void Dynamite::summon_lament(int index)
{
	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	do_command(SELF, Position::Hand, index, Command::Summon);

	int SECONDS = 0;

	while (get_card_at(SELF, Position::MZ_2, 0) != CardID::Lilith_Lady_of_Lament)
	{
		if (DUEL_END) return;
		if (USED_THOUSAND && (SECONDS > 5))
		{
			std::cout << "::: LAMENT (DISABLED)" << std::endl;
			return;
		}
		Sleep(1000);
		SECONDS++;
	}

	NORMAL_SUMMONED = true;

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	while (get_chain_number() > 0)
	{
		if (DUEL_END) return;
		if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
		{
			use_lament();
			STATE = State::SET_TRAP;
			send_right_click();
			return;
		}
		else SendESC();
		Sleep(1000);
	}
}

void Dynamite::use_lament()
{
	std::cout << "::: LAMENT (USE)" << std::endl;
	std::array<int, 5> PREV_STZ = { get_card_at(SELF, Position::STZ_0, 0),  get_card_at(SELF, Position::STZ_1, 0),  get_card_at(SELF, Position::STZ_2, 0),  get_card_at(SELF, Position::STZ_3, 0),  get_card_at(SELF, Position::STZ_4, 0) };
	std::array<int, 5> CURR_STZ = PREV_STZ;

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	do_command(SELF, Position::MZ_2, 0, Command::Action);
	Sleep(2000);
	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	do_command(SELF, Position::MZ_2, 0, Command::Decide);

	USED_LAMENT = true;

	while (!is_dialog())
	{
		if (DUEL_END) return;

		if (get_chain_number() > 0)
		{
			if (get_chain_number() == 2)
			{
				if (get_card_atByChainId(2) == CardID::PSY_Framegear_Gamma || get_card_atByChainId(2) == CardID::Herald_of_Orange_Light)
				{
					std::cout << "::: LAMENT (NEGATED)" << std::endl;
					STATE = State::SET_TRAP;
					send_right_click();
					return;
				}
			}
			else if (get_chain_number() == 3)
			{
				if (get_card_atByChainId(3) == CardID::PSY_Framegear_Gamma || get_card_atByChainId(2) == CardID::Herald_of_Orange_Light)
				{
					std::cout << "::: LAMENT (NEGATED)" << std::endl;
					STATE = State::SET_TRAP;
					send_right_click();
					return;
				}
			}
			else
			{
				send_right_click();
			}
		}

		Sleep(1000);
	}

	// Count
	int traps_in_deck = 9;

	for (int i = 0; i < hand_count(SELF); i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::D_D_Dynamite) traps_in_deck--;
		else if (get_card_at(SELF, Position::Hand, i) == CardID::Banquet_of_Millions) traps_in_deck--;
		else if (get_card_at(SELF, Position::Hand, i) == CardID::Trap_Trick) traps_in_deck--;
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::D_D_Dynamite) traps_in_deck--;
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Banquet_of_Millions) traps_in_deck--;
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Trap_Trick) traps_in_deck--;
	}

	for (int i = 0; i < (card_top_of(SELF, Position::Graveyard) + 1); i++)
	{
		if (get_card_at(SELF, Position::Graveyard, i) == CardID::D_D_Dynamite) traps_in_deck--;
		else if (get_card_at(SELF, Position::Graveyard, i) == CardID::Banquet_of_Millions) traps_in_deck--;
		else if (get_card_at(SELF, Position::Graveyard, i) == CardID::Trap_Trick) traps_in_deck--;
	}

	// Select
	if (!SET_BANQUET)
	{
		send_scroll(960, 860, 10);
		Sleep(500);
		select_lament_right(0);
		Sleep(500);
		select_lament_right(1);
		Sleep(500);
		select_lament_right(2);
		Sleep(500);
		select_confirm();
	}
	else
	{
		select_card(traps_in_deck, 0, Mode::Skip);
		Sleep(500);
		select_card(traps_in_deck, 1, Mode::Skip);
		Sleep(500);
		select_card(traps_in_deck, 2);
	}

	while (PREV_STZ == CURR_STZ)
	{
		if (DUEL_END) return;
		CURR_STZ = { get_card_at(SELF, Position::STZ_0, 0),  get_card_at(SELF, Position::STZ_1, 0),  get_card_at(SELF, Position::STZ_2, 0),  get_card_at(SELF, Position::STZ_3, 0),  get_card_at(SELF, Position::STZ_4, 0) };
		Sleep(1000);
	}

	for (int i = 0; i < 5; i++)
	{
		if (PREV_STZ[i] != CURR_STZ[i])
		{
			if (CURR_STZ[i] == CardID::D_D_Dynamite)
			{
				std::cout << "::: LAMENT (DYNAMITE)" << std::endl;
				DYNAMITE_COUNT++;
			}
			else if (CURR_STZ[i] == CardID::Trap_Trick)
			{
				std::cout << "::: LAMENT (TRICK)" << std::endl;
				SET_TRICK = true;
			}
			else if (CURR_STZ[i] == CardID::Banquet_of_Millions)
			{
				std::cout << "::: LAMENT (BANQUET)" << std::endl;
				SET_BANQUET = true;
			}
			break;
		}
	}

	if (can_win())
	{
		STATE = State::WIN;
	}
}

void Dynamite::use_demise()
{
	USED_DEMISE = true;

	int MONSTERS = 0;

	for (int i = 0; i < hand_count(SELF); i++)
	{
		if (CardID::Lilith_Lady_of_Lament == get_card_at(SELF, Position::Hand, i)) MONSTERS++;
	}

	int ZONES = 0;

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == 0) ZONES++;
	}

	int CAN_SET = min((hand_count(SELF) - MONSTERS), ZONES);

	while (CAN_SET != 0)
	{
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (CardID::Lilith_Lady_of_Lament != get_card_at(SELF, Position::Hand, i))
			{
				SetSTZ(SELF, i);
				while (an_action() == 0)
				{
					if (DUEL_END) return;
					Sleep(1000);
				}
				break;
			}
		}
		CAN_SET--;
	}

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Card_of_Demise)
		{
			ActivateSpell(SELF, i);
			Sleep(5000);
			break;
		}
	}

	while (hand_count(SELF) != 3)
	{
		if (DUEL_END) return;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return;
					DUEL_END = true;
					Sleep(1000);
				}
				return;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	for (int i = 0; i < 3; i++)
	{
		bool resolved = false;
		for (int j = 0; j < 5; j++)
		{
			if (get_card_at(SELF, Position::STZ_0 + j, 0) == CardID::Into_the_Void)
			{
				resolved = use_void(0, Position::STZ_0 + j);
				break;
			}
		}
		if (!resolved) break;
	}

	STATE = State::SET_TRAP;

	for (int i = 0; i < hand_count(SELF); i++)
	{
		if (get_card_at(SELF, Position::Hand, i) == CardID::Into_the_Void)
		{
			STATE = State::USE_SPELL;
			break;
		}
	}
}

void Dynamite::use_toon(int index)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	do_command(SELF, Position::Hand, index, Command::Action);

	if (TOON_TABLE_IN_DECK == 3)
	{
		TOON_TABLE_IN_DECK = 2;
	}

	while (!is_dialog())
	{
		if (DUEL_END) return;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return;
					SendESC();
					Sleep(1000);
				}
				return;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	if (TOON_TABLE_IN_DECK > 0) TOON_TABLE_IN_DECK--;

	select_card(TOON_TABLE_IN_DECK + TOON_WORLD_IN_DECK + 1, TOON_TABLE_IN_DECK + TOON_WORLD_IN_DECK);

	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return;
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}
}

bool Dynamite::use_void(int index, int position)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	do_command(SELF, position, index, Command::Action);
	
	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
				PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
			}
			else
			{
				send_right_click();
			}
		}
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	return true;
}

bool Dynamite::use_thousand(int index)
{
	int PREV_LP = GetLP(RIVAL);

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	do_command(SELF, Position::Hand, index, Command::Action);

	USED_THOUSAND = true;

	while (PREV_LP == GetLP(RIVAL))
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	while (get_chain_number() != 0)
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	return true;
}

bool Dynamite::use_upstart(int index, int position)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	do_command(SELF, position, index, Command::Action);

	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
				PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
			}
			else
			{
				send_right_click();
			}
		}
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	return true;
}

bool Dynamite::use_duality(int index)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	do_command(SELF, Position::Hand, index, Command::Action);

	USED_DUALITY = true;

	while (!is_dialog())
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
				PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	select_card(3, get_duality_index());

	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return true;
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		Sleep(1000);
	}

	while (hand_count(SELF) == 0)
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	return true;
}

bool Dynamite::use_moon(int index, int position)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	do_command(SELF, position, index, Command::Action);

	USED_MOON = true;

	while (!is_dialog())
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
				PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	std::vector<int> PREV_HAND = GetHand(SELF);

	select_card(hand_count(SELF));
	Sleep(500);
	select_card(hand_count(SELF));	

	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return true;
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		Sleep(1000);
	}

	if (std::find(PREV_HAND.begin(), PREV_HAND.end(), CardID::Banquet_of_Millions) != PREV_HAND.end())
	{
		std::vector<int> CURR_HAND = GetHand(SELF);
		if (std::find(CURR_HAND.begin(), CURR_HAND.end(), CardID::Banquet_of_Millions) == PREV_HAND.end())
		{
			std::cout << "::: BANQUET (MOON)" << std::endl;
			REMOVE_BANQUET_FROM_TRICK = true;
		}
	}


	if (!TOON_WORLD_IN_DECK && !TOON_WORLD_REMOVED)
	{
		TOON_WORLD_REMOVED = true;
	}

	while (hand_count(SELF) == 0)
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	if (hand_count(SELF) >= 3)
	{
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (get_card_at(SELF, Position::Hand, i) == Into_the_Void) return false;
		}
	}

	return true;
}

bool Dynamite::use_popperup(int index, int position)
{
	do_command(SELF, position, index, Command::Action);

	USED_POPPERUP = true;

	while (!is_dialog())
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	int SENT = 1;
	select_card(hand_count(SELF));
	while (hand_count(SELF) > 0 && SENT < 3)
	{
		if (DUEL_END) return true;
		select_card(hand_count(SELF) - SENT, 0, Mode::Optional);
		SENT++;
	}
	select_confirm();

	if (!TOON_WORLD_IN_DECK && !TOON_WORLD_REMOVED)
	{
		TOON_WORLD_REMOVED = true;
	}

	while (an_action() == 0)
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return true;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	while (hand_count(SELF) == 0)
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}


	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	if (hand_count(SELF) >= 3)
	{
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (get_card_at(SELF, Position::Hand, i) == Into_the_Void) return false;
		}
	}

	return true;
}

bool Dynamite::use_reload(int index)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	do_command(SELF, Position::Hand, index, Command::Action);

	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
				PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
			}
			else
			{
				send_right_click();
			}
		}
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		Sleep(1000);
	}

	if (!TOON_WORLD_IN_DECK && !TOON_WORLD_REMOVED)
	{
		TOON_WORLD_IN_DECK = true;
	}

	while (hand_count(SELF) == 0)
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	if (hand_count(SELF) >= 3)
	{
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (get_card_at(SELF,Position::Hand,i) == CardID::Into_the_Void)
			return false;
		}
	}

	return true;
}

bool Dynamite::use_mallet(int index, int position)
{
	int PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
	int CURR_GY = PREV_GY;

	do_command(SELF, position, index, Command::Action);

	while (!is_dialog())
	{
		if (DUEL_END) return true;
		if (get_chain_number() == 2)
		{
			if (get_card_atByChainId(2) == CardID::Ash_Blossom)
			{
				while (get_chain_number() > 0)
				{
					if (DUEL_END) return true;
					SendESC();
					Sleep(1000);
				}
				return false;
			}
			else if (get_card_atByChainId(get_chain_number()) == CardID::Effect_Veiler || get_card_atByChainId(get_chain_number()) == CardID::Infinite_Impermanence)
			{
				use_lament();
				PREV_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	while (PREV_GY == CURR_GY)
	{
		if (DUEL_END) return true;
		CURR_GY = (get_card_at(SELF, Position::Graveyard, 0) == 0) ? 0 : card_top_of(SELF, Position::Graveyard) + 1;
		do_command(SELF, Position::Hand, 0, Command::Decide);
		Sleep(1000);
	}

	if (!TOON_WORLD_IN_DECK && !TOON_WORLD_REMOVED)
	{
		TOON_WORLD_IN_DECK = true;
	}

	while (hand_count(SELF) == 0)
	{
		if (DUEL_END) return true;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return true;
		if (get_card_at(RIVAL, Position::Graveyard, card_top_of(RIVAL, Position::Graveyard)) == CardID::Droll_Lock_Bird) DUEL_END = true;
		Sleep(1000);
	}

	if (hand_count(SELF) >= 3)
	{
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (get_card_at(SELF, Position::Hand, i) == CardID::Into_the_Void)
				return false;
		}
	}

	return true;
}

int Dynamite::get_duality_index()
{

	std::vector<int> EXCAVATE = ExcavateThree(SELF);

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Lilith_Lady_of_Lament && !NORMAL_SUMMONED) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Banquet_of_Millions && !SET_BANQUET) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::D_D_Dynamite && (DYNAMITE_COUNT + SET_TRICK) <= 1) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Trap_Trick && !SET_TRICK && DYNAMITE_COUNT <= 1) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Card_of_Demise && !SET_DEMISE) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Toon_Table_of_Contents && (TOON_TABLE_IN_DECK + (TOON_WORLD_IN_DECK && !TOON_WORLD_REMOVED)) >= 4) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Into_the_Void && hand_count(SELF) >= 2) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Contract_with_Don_Thousand && !USED_THOUSAND && free_stz(SELF) > 1 ) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Upstart_Goblin && GetLP(RIVAL) <= UPSTART_LP_CEILING) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Performapal_Popperup && !USED_POPPERUP && hand_count(SELF) >= 3) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Gold_Moon_Coin && !USED_MOON && hand_count(SELF) >= 2) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Performapal_Popperup && !USED_POPPERUP && hand_count(SELF) >= 1) return i;
	
	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Reload && hand_count(SELF) >= 1) return i;

	for (int i = 0; i < 3; i++) if (EXCAVATE[i] == CardID::Magical_Mallet && hand_count(SELF) >= 1) return i;

}

bool Dynamite::can_win()
{
	if (DYNAMITE_COUNT == 0) return ((NORMAL_SUMMONED && !USED_LAMENT) && SET_BANQUET && SET_TRICK);
	if (DYNAMITE_COUNT == 1) return ((NORMAL_SUMMONED && !USED_LAMENT) && SET_TRICK) || (SET_BANQUET && ((NORMAL_SUMMONED && !USED_LAMENT) || SET_TRICK));
	if (DYNAMITE_COUNT == 2) return ((NORMAL_SUMMONED && !USED_LAMENT) || SET_BANQUET);
	return false;
}

bool Dynamite::is_win()
{
	if (DYNAMITE_COUNT == 2 && SET_BANQUET) return true;
	if (DYNAMITE_COUNT == 1 && SET_BANQUET && SET_TRICK) return true;
	return false;
}

void Dynamite::win()
{
	//std::cout << "--- WIN   ---" << std::endl << std::endl;

	// Case where banquet is in hand, but we didn't play it due to not getting banquet from lament
	if (!SET_BANQUET)
	{
		for (int i = 0; i < hand_count(SELF); i++)
		{
			if (get_card_at(SELF, Position::Hand, i) == CardID::Banquet_of_Millions)
			{
				std::cout << "::: BANQUET (EXTRA)" << std::endl;
				SetSTZ(SELF, i);
				while (an_action() == 0)
				{
					if (DUEL_END) return;
					Sleep(1000);
				}
				SET_BANQUET = true;
			}
		}
	}

	//std::cout << "::: BEFORE" << std::endl;
	while (an_action() == 0)
	{
		if (DUEL_END) return;
		send_right_click();
		Sleep(1000);
	}
	//std::cout << "::: AFTER" << std::endl;

	std::cout << "::: TOGGLE OFF" << std::endl;
	click_toggle();
	Sleep(500);

	std::cout << "::: END TURN" << std::endl;
	click_phase();
	Sleep(500);
	select_phase(Phase::End);
	Sleep(500);
	
	while (current_turn() != RIVAL)
	{
		if (DUEL_END) return;
		send_right_click();
		Sleep(1000);
	}

	std::cout << "::: TOGGLE ON" << std::endl;
	click_toggle();
	Sleep(500);

	while (GetCurrentPhase() != Phase::Draw)
	{
		if (DUEL_END) return;
		Sleep(1000);
	}



	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Banquet_of_Millions)
		{
			std::cout << "::: BANQUET" << std::endl;
			
			while (!can_do_action())
			{
				if (DUEL_END) return;
				Sleep(1000);
			}

			std::vector<int> stz = get_live_stz();
			if (stz.size() > 1)
			{
				//std::cout << "::: OPTIONS - " << stz.size() << std::endl;
				while (!is_response())
				{
					if (DUEL_END) return;
					Sleep(1000);
				}

				int INDEX = 0;
				for (int i = 0; i < stz.size(); i++)
				{
					if (stz[i] == CardID::Banquet_of_Millions)
					{
						INDEX = i;
						break;
					}
				}

				int cards_in_stz = static_cast<int>(stz.size());

				select_card(cards_in_stz, INDEX, 2);
				Sleep(2000);
				// Cancel response

				while (!is_response())
				{
					if (DUEL_END) return;
					Sleep(1000);
				}

				Sleep(1000);

				send_right_click();
				Sleep(2000);
			}
			else
			{
				//std::cout << "::: MIDDLE CARD" << std::endl;

				// Wait for reponse to use middle card
				while (!is_response())
				{
					if (DUEL_END) return;
					
					Sleep(1000);
				}
				Sleep(1000);
				send_click(958, 860);
				Sleep(2000);
				//std::cout << "::: BANISH (CONFIRM)" << std::endl;
				send_click(980, 1005);
			}
			break;
		}
	}

	//std::cout << "::: BANQUET (RESOLVE)" << std::endl;
	
	while (!is_dialog())
	{
		if (DUEL_END) return;
		if (get_chain_number() == 2)
		{
			if (STZ_REMOVAL.find(get_card_atByChainId(2)) != STZ_REMOVAL.end())
			{
				DUEL_END = true;
				return;
			}
			else
			{
				send_right_click();
			}
		}
		Sleep(1000);
	}

	//std::cout << "::: BANISH" << std::endl;

	Sleep(1000);

	select_banquet_banish();

	Sleep(1000);

	send_move(965, 1020);

	Sleep(1000);

	select_confirm();

	Sleep(2000);

	//std::cout << "::: WHILE ANYTHING" << std::endl;

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	//std::cout << "::: WHILE GY" << std::endl;

	while (get_card_at(SELF, Position::Graveyard, card_top_of(SELF, Position::Graveyard)) != CardID::Banquet_of_Millions)
	{
		if (DUEL_END) return;
		if (get_card_at(SELF, Position::Graveyard, card_top_of(SELF, Position::Graveyard) - 1) == CardID::Banquet_of_Millions)
		{
			break;
		}
		Sleep(1000);
	}

	//std::cout << "::: WHILE ANYTHING 2" << std::endl;

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::D_D_Dynamite)
		{
			std::cout << "::: DYNAMITE #1" << std::endl;

			do_command(SELF, Position::STZ_0 + i, 0, Command::Action);
			
			break;
		}
	}

	while (get_chain_number() != 1)
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	while (!can_do_action())
	{
		if (DUEL_END) return;
		Sleep(1000);
	}

	if (SET_TRICK)
	{
		for (int i = 0; i < 5; i++)
		{
			if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Trap_Trick)
			{
				std::cout << "::: TRICK" << std::endl;

				do_command(SELF, Position::STZ_0 + i, 0, Command::Action);

				//std::cout << "::: DIALOG" << std::endl;

				while (!is_dialog())
				{
					if (DUEL_END) return;
					Sleep(1000);
				}

				int dynamites_in_deck = 2;
				int banquets_in_deck = 3;

				for (int i = 0; i < (card_top_of(SELF, Position::Graveyard) + 1); i++)
				{
					if (get_card_at(SELF, Position::Graveyard, i) == CardID::D_D_Dynamite) dynamites_in_deck--;
					if (get_card_at(SELF, Position::Graveyard, i) == CardID::Banquet_of_Millions) banquets_in_deck--;
				}

				for (int i = 0; i < hand_count(SELF); i++)
				{
					if (get_card_at(SELF, Position::Hand, i) == CardID::D_D_Dynamite) dynamites_in_deck--;
					if (get_card_at(SELF, Position::Hand, i) == CardID::Banquet_of_Millions) banquets_in_deck--;
				}


				if (dynamites_in_deck < 2) dynamites_in_deck = 0;
				if (banquets_in_deck < 2) banquets_in_deck = 0;

				if (REMOVE_BANQUET_FROM_TRICK) banquets_in_deck = 0;

				select_card(dynamites_in_deck + banquets_in_deck);

				//std::cout << "::: RESOLVE" << std::endl;

				while (get_card_at(SELF, Position::Graveyard, card_top_of(SELF, Position::Graveyard)) != CardID::D_D_Dynamite)
				{
					if (DUEL_END) return;
					Sleep(1000);
				}

				while (!can_do_action())
				{
					if (DUEL_END) return;
					Sleep(1000);
				}

				break;
			}
		}
	}

	for (int i = 4; i >= 0; i--)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::D_D_Dynamite)
		{
			std::cout << "::: DYNAMITE #2" << std::endl;

			do_command(SELF, Position::STZ_0 + i, 0, Command::Action);
			
			break;
		}
	}

	while (GetLP(RIVAL) != 0)
	{
		if (DUEL_END) return;
		Sleep(1000);
		send_right_click();
	}
	
	std::cout << "::: 0 LP (WIN)" << std::endl;

	DUEL_END = true;
}

std::vector<int> Dynamite::get_live_stz()
{
	std::vector<int> stz = {};
	for (int i = 0; i < 5; i++)
	{
		if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::D_D_Dynamite && ((get_card_at(RIVAL, Position::Exclude, 0) == 0) ? 0 : card_top_of(RIVAL, Position::Exclude) + 1))
		{
			stz.push_back(CardID::D_D_Dynamite);
		}
		else if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Banquet_of_Millions && card_top_of(RIVAL, Position::Extra_Deck) != 0)
		{
			stz.push_back(CardID::Banquet_of_Millions);
		}
		else if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Trap_Trick && free_stz(SELF))
		{
			stz.push_back(CardID::Trap_Trick);
		}
		else if (get_card_at(SELF, Position::STZ_0 + i, 0) == CardID::Reload && hand_count(SELF) > 0)
		{
			stz.push_back(CardID::Reload);
		}
	}
	return stz;
}
