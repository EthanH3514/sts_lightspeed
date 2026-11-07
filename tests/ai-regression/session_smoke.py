"""Smoke the built Python module, including independent exact-clone ownership.

Usage: python tests/ai-regression/session_smoke.py /absolute/path/to/build
On Windows deploy the compiler runtime DLLs next to the module first.
"""
from contextlib import ExitStack
import json
import os
from pathlib import Path
import sys


def main():
    build = Path(sys.argv[1]).resolve()
    sys.path.insert(0, str(build))
    with ExitStack() as stack:
        if hasattr(os, 'add_dll_directory'):
            stack.enter_context(os.add_dll_directory(str(build)))
        import slaythespire as sim
        cases = 0
        for ascension in (0, 10, 20):
            for seed in (1, 17, 123, 999):
                original = sim.SimulatorSession()
                original.reset(sim.CharacterClass.IRONCLAD, seed, ascension)
                before = original.get_state()
                before_actions = original.get_actions_text()
                event = json.loads(original.public_event_context())
                assert event == dict(version='public-event-context-v1', available=True,
                                     event_name='NEOW', act=1, floor=0)
                assert original.get_state() == before  # Export consumes no RNG.
                assert before['run']['ascension'] == ascension
                assert before['map']['nodes']
                supplement = json.loads(original.public_run_supplement())
                assert supplement['version'] == 'public-run-supplement-v1'
                assert len(supplement['deck']) == (11 if ascension >= 10 else 10)
                assert all(not card['bottled_attack'] and not card['bottled_skill']
                           and not card['bottled_power'] for card in supplement['deck'])
                assert json.loads(original.public_shop_shelf()) == {
                    'version': 'public-shop-shelf-v1', 'available': False}
                assert '0:' in before_actions
                left, right = original.clone_exact(), original.clone_exact()
                assert left.get_state() == before == right.get_state()
                assert left.public_event_context() == original.public_event_context() == right.public_event_context()
                left.take_action('0')
                assert original.get_state() == before
                assert original.get_actions_text() == before_actions
                assert right.get_state() == before
                right.take_action('0')
                assert left.get_state() == right.get_state()
                assert left.get_actions_text() == right.get_actions_text()
                assert left.get_state() != before
                for invalid in (-1, 21):
                    try:
                        sim.SimulatorSession().reset(sim.CharacterClass.IRONCLAD, seed, invalid)
                    except ValueError:
                        pass
                    else:
                        raise AssertionError('invalid ascension accepted')
                cases += 1
        print(json.dumps({'status': 'passed', 'session_cases': cases,
                          'optional_session_search': hasattr(sim.SimulatorSession, 'search_session_public')}))


if __name__ == '__main__':
    main()
