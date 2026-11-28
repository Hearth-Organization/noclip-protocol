"""Street lamps that light (Prefabs/Landmarks/StreetlampLit.prefab) and that flicker (StreetlampFlicker.prefab):
the Streetlamp landmark with a sodium-orange light under its head, taken from Level 0's ceiling light pieces.
Level 9 (the wiki: most lamps are dead, a few flicker or come on for a moment) uses all three.

usage: python3 Tools/Levels/make_streetlamps.py
"""
import copy
import json
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
P = os.path.join(ROOT, 'Prefabs')
HEAD = 4.85          # the light hangs just under the lamp's head (the model is 5 m tall)
SODIUM = [1.0, 0.72, 0.42]
rng = random.Random(9)


def load(path):
    return json.load(open(os.path.join(P, path)))


def new_id():
    return 'Entity_[%d]' % rng.randrange(10**12, 10**13)


def take(source, names):
    """The named entities of a prefab, under fresh ids, re-parented to each other as before."""
    ids = {k: new_id() for k in source['Entities']}
    out = {}
    for k, e in source['Entities'].items():
        if e['Name'] not in names:
            continue
        e = copy.deepcopy(e)
        e['Id'] = ids[k]
        for c in e['Components'].values():
            if 'TransformComponent' in c.get('$type', '') and c.get('Parent Entity') in ids:
                c['Parent Entity'] = ids[c['Parent Entity']]
        out[ids[k]] = e
    return out, ids


def light_up(entities, root_name):
    for e in entities.values():
        for c in e['Components'].values():
            t = c.get('$type', '')
            if 'TransformComponent' in t and e['Name'] == root_name:
                c['Parent Entity'] = 'ContainerEntity'
                c['Transform Data'] = {'Translate': [0.0, 0.0, HEAD]}
            if 'TransformComponent' in t and e['Name'] != root_name:
                data = c.setdefault('Transform Data', {})
                data['Translate'] = [0.0, 0.0, -0.05]
            if 'AreaLight' in t:
                cfg = c['Controller']['Configuration']
                cfg['Color'] = SODIUM
                # A street lamp: a wide cone over the road, farther than a ceiling panel.
                cfg['Intensity'] = cfg.get('Intensity', 100.0) * 1.8
                cfg['AttenuationRadius'] = 16.0
            if 'MiniAudio' in t:
                c['Controller']['Config']['Max Distance'] = 8.0


def make(name, light_prefab, keep, root_name):
    lamp = load('Landmarks/Streetlamp.prefab')
    entities, ids = take(load(light_prefab), keep)
    # The flicker component names its panels by entity id; this lamp has none (the lamp model is the fixture).
    for e in entities.values():
        for c in e['Components'].values():
            t = c.get('m_template', {})
            if t.get('$type') == 'FlickerLightComponent':
                t['LightEntity'] = ids.get(t.get('LightEntity', ''), '')
                t.pop('PanelEntity', None)
                t.pop('DarkPanelEntity', None)
    light_up(entities, root_name)
    lamp['ContainerEntity']['Name'] = name
    lamp['Entities'] = entities
    json.dump(lamp, open(os.path.join(P, 'Landmarks', name + '.prefab'), 'w'), indent=4)
    print('wrote', name, len(entities), 'entities')


make('StreetlampLit', 'Level0/L0_Light.prefab', {'L0_LightRoot', 'L0_LightBulb', 'L0_LightFill'}, 'L0_LightRoot')
make('StreetlampFlicker', 'Level0/L0_LightFlicker.prefab', {'L0_LightFlickerRoot', 'L0_FlickerBulb'}, 'L0_LightFlickerRoot')
