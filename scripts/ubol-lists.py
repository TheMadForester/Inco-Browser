#!/usr/bin/env python3
"""Turn the default uBlock Origin Lite rulesets into resources/filter-ubol.txt.

Usage: scripts/ubol-lists.py <unpacked uBOL folder> [output]

Only host-level rules are kept, because Inco Filter decides per host and
request type. Kept: block and allow rules whose target is a plain domain
(requestDomains, or a urlFilter of the form ||example.com^), together with
their resourceTypes, initiatorDomains, excludedInitiatorDomains, domainType
and priority. Everything else is skipped and counted on stderr.

Output, one rule per line:
    host [option ...]
    @@host [option ...]          allow rule
Options:
    types=a,b   not-types=a,b    request types (DNR names)
    sites=a|b   not-sites=a|b    page (initiator) domains
    third-party first-party      domainType
    priority=N                   left out when it is 10
A line without types= or not-types= matches every type except main_frame,
which is the DNR default.
"""

import collections
import json
import os
import re
import sys

HOST_FILTER = re.compile(r'^\|\|([a-z0-9-]+(?:\.[a-z0-9-]+)+)\^$')
HOST_PREFIX = re.compile(r'^\|\|([a-z0-9-]+(?:\.[a-z0-9-]+)+)[/^]')
DOMAIN = re.compile(r'^[a-z0-9-]+(?:\.[a-z0-9-]+)+$')
KNOWN_KEYS = {'urlFilter', 'requestDomains', 'excludedRequestDomains', 'resourceTypes',
              'excludedResourceTypes', 'initiatorDomains', 'excludedInitiatorDomains',
              'domainType'}
DEFAULT_PRIORITY = 10
# uBOL's metadata has no license field, so these come from each list's site.
LICENSES = {
    'ublock-filters': 'GPLv3',
    'ublock-badware': 'GPLv3',
    'easylist': 'GPLv3 or CC BY-SA 3.0, the EasyList authors',
    'easyprivacy': 'GPLv3 or CC BY-SA 3.0, the EasyList authors',
    'pgl': 'Peter Lowe, free to combine and republish per the list page',
    'urlhaus-full': 'CC0 / MIT, malware-filter; URLhaus data CC0',
}

skipped = collections.Counter()


def is_under(host, parent):
    return host == parent or host.endswith('.' + parent)


def options(cond, priority):
    opts = []
    if cond.get('resourceTypes'):
        opts.append('types=' + ','.join(sorted(cond['resourceTypes'])))
    if cond.get('excludedResourceTypes'):
        opts.append('not-types=' + ','.join(sorted(cond['excludedResourceTypes'])))
    if cond.get('initiatorDomains'):
        opts.append('sites=' + '|'.join(sorted(cond['initiatorDomains'])))
    if cond.get('excludedInitiatorDomains'):
        opts.append('not-sites=' + '|'.join(sorted(cond['excludedInitiatorDomains'])))
    if cond.get('domainType') == 'thirdParty':
        opts.append('third-party')
    elif cond.get('domainType') == 'firstParty':
        opts.append('first-party')
    if priority != DEFAULT_PRIORITY:
        opts.append('priority=%d' % priority)
    return ' '.join(opts)


def rule_hosts(rule):
    """Hosts a rule applies to, or None when it is not a host-level rule."""
    cond = rule['condition']
    if set(cond) - KNOWN_KEYS:
        return None
    url = cond.get('urlFilter')
    if url is not None:
        m = HOST_FILTER.match(url)
        if not m or cond.get('requestDomains'):
            return None
        hosts = [m.group(1)]
    else:
        hosts = cond.get('requestDomains') or []
    hosts = [h for h in hosts if DOMAIN.match(h)]
    # "block a.com except b.a.com": keep only the hosts no exclusion sits under.
    excluded = cond.get('excludedRequestDomains') or []
    if excluded:
        kept = [h for h in hosts if not any(is_under(x, h) for x in excluded)]
        skipped['hosts dropped for excludedRequestDomains'] += len(hosts) - len(kept)
        hosts = kept
    return hosts


def convert(folder):
    base = os.path.join(folder, 'rulesets')
    with open(os.path.join(base, 'ruleset-details.json')) as f:
        details = json.load(f)
    chosen = [d for d in details if d.get('enabled')]

    lines = {}   # (host, opts, allow) -> None, keeps first-seen order
    for d in chosen:
        with open(os.path.join(base, 'main', d['id'] + '.json')) as f:
            rules = json.load(f)
        for rule in rules:
            kind = rule['action']['type']
            if kind not in ('block', 'allow'):
                skipped[kind + ' rules'] += 1
                continue
            cond = rule['condition']
            hosts = rule_hosts(rule)
            if hosts is None:
                # A path-level allow limited to some sites becomes a host
                # allow on those sites, so the host block cannot break them.
                url = cond.get('urlFilter') or ''
                m = HOST_PREFIX.match(url)
                if kind == 'allow' and m and cond.get('initiatorDomains') \
                        and not set(cond) - KNOWN_KEYS:
                    hosts = [m.group(1)]
                else:
                    skipped[kind + ' rules with path, pattern or regex'] += 1
                    continue
            opts = options(cond, rule.get('priority', 1))
            for h in hosts:
                lines[(h, opts, kind == 'allow')] = None

    # A plain block under a plain parent block adds nothing.
    plain = {h for (h, opts, allow) in lines if not opts and not allow}
    out = []
    for (h, opts, allow) in lines:
        if not opts and not allow:
            parts = h.split('.')
            if any('.'.join(parts[i:]) in plain for i in range(1, len(parts) - 1)):
                skipped['plain hosts already covered by a parent'] += 1
                continue
        out.append(('@@' if allow else '') + h + (' ' + opts if opts else ''))
    return chosen, out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    folder = sys.argv[1]
    target = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'resources', 'filter-ubol.txt')
    with open(os.path.join(folder, 'manifest.json')) as f:
        version = json.load(f)['version']
    chosen, out = convert(folder)

    with open(target, 'w') as f:
        f.write('# Inco Filter: host rules taken from uBlock Origin Lite %s\n' % version)
        f.write('# https://github.com/uBlockOrigin/uBOL-home\n')
        f.write('# uBlock Origin Lite: GPLv3, Raymond Hill and the uBlock Origin contributors.\n')
        f.write('# Converted by scripts/ubol-lists.py, host-level rules only. Rulesets:\n')
        for d in chosen:
            f.write('#   %s (%s)\n' % (d['name'], d.get('homeURL', '')))
            if d['id'] in LICENSES:
                f.write('#     license: %s\n' % LICENSES[d['id']])
        f.write('# The rules come from those lists and stay under their authors\' licenses.\n')
        f.write('# Not included: path, pattern and regex rules, redirect and header rules,\n')
        f.write('# cosmetic filters and scriptlets. Network blocking by host only.\n')
        f.write('# Format: see scripts/ubol-lists.py.\n')
        for line in out:
            f.write(line + '\n')

    print('rulesets: ' + ', '.join(d['id'] for d in chosen), file=sys.stderr)
    print('lines written: %d (%d allow)' % (len(out), sum(l.startswith('@@') for l in out)),
          file=sys.stderr)
    for what, n in skipped.most_common():
        print('skipped %s: %d' % (what, n), file=sys.stderr)


if __name__ == '__main__':
    main()
