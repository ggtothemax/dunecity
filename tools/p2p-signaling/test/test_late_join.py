"""Late-join requests confer no membership until the host approves a bounded grant."""
import json
from pathlib import Path
import unittest
from test_signaling import SignalingTestCase, claims, APP_VERSION

class LateJoinTests(SignalingTestCase):
    def running(self, **fields):
        a=self.service.request('POST','/v1/admission/host',claims(**dict(dict(visibility='public',maxPeers=4,mode='custom',allowLateJoin=1,map='Test map'.encode().hex()),**fields)))
        h=self.session(a.fields['grant'],'Host',**{k:v for k,v in fields.items() if k in ('gameProtocol','appVersion','contentHash')})
        self.assertEqual(200,self.phase(h.fields['session'],'match').status)
        return a,h
    def request(self,a,name='Guest',**fields):
        return self.service.request('POST','/v1/admission/request',claims(room=a.fields['room'],name=name.encode().hex(),publicOnly=1,**fields))
    def status(self,ticket,**fields):
        return self.service.request('POST','/v1/admission/request-status',claims(request=ticket,**fields))
    def manage(self,h,action='list',request=''):
        return self.service.request('POST','/v1/p2p/join-requests',dict(action=action,**({'request':request} if request else {})),headers={'X-Dune-Session':h.fields['session']})
    def test_running_directory_is_opt_in_and_carries_map_time(self):
        a,h=self.running()
        page=self.service.request('POST','/v1/admission/list',claims(offset=0,allMods=1,details=1))
        row=page.multi['game'][0].split('|')
        self.assertEqual(11,len(row)); self.assertEqual('Test map',bytes.fromhex(row[7]).decode())
        self.assertEqual(['match','0','1'],row[8:])
        self.assertNotIn('game',self.service.request('POST','/v1/admission/list',claims(offset=0,allMods=1)).fields)
        self.assertEqual(409,self.join(a.fields['room']).status)
    def test_host_approval_issues_name_bound_grant_and_does_not_reopen_general_admission(self):
        a,h=self.running(); r=self.request(a); self.assertEqual(200,r.status)
        ticket=r.fields['request']; self.assertEqual('pending',self.status(ticket).fields['requestState'])
        queue=self.manage(h); self.assertEqual(200,queue.status)
        request=queue.multi['request'][0].split('|')[0]
        self.assertEqual(200,self.manage(h,'approve',request).status)
        approved=self.status(ticket); self.assertEqual('approved',approved.fields['requestState'])
        self.assertEqual(409,self.join(a.fields['room']).status)
        g=self.session(approved.fields['grant'],'Guest'); self.assertEqual(200,g.status)
        self.assertEqual(403,self.manage(g).status)
        self.assertEqual('joined',self.status(ticket).fields['requestState'])
        self.assertEqual(200,self.phase(h.fields['session'],'match').status)
    def test_hot_join_version_mismatch_never_reaches_the_host_queue(self):
        a,h=self.running()
        refused=self.request(a,appVersion='9.9.9')
        self.assertEqual(409,refused.status)
        self.assertEqual('version_mismatch',refused.fields['code'])
        self.assertIn(APP_VERSION,refused.fields['message'])
        self.assertIn('9.9.9',refused.fields['message'])
        self.assertNotIn('request',self.manage(h).multi)
        self.assertEqual(200,self.request(a).status)
    def test_a_wrong_name_cannot_redeem_an_approved_grant(self):
        a,h=self.running(); r=self.request(a); request=self.manage(h).multi['request'][0].split('|')[0]
        self.manage(h,'approve',request)
        self.assertNotEqual(200,self.session(self.status(r.fields['request']).fields['grant'],'Other name').status)
    def test_decline_cancel_and_mismatching_claims(self):
        a,h=self.running(); r=self.request(a)
        self.assertEqual(409,self.request(a,contentHash='b'*64).status)
        self.assertEqual(403,self.status(r.fields['request'],runtime='browser').status)
        self.assertEqual('cancelled',self.status(r.fields['request'],cancel=1).fields['requestState'])
        r=self.request(a,'Another'); request=self.manage(h).multi['request'][0].split('|')[0]
        self.manage(h,'decline',request)
        self.assertEqual('pending',self.status(r.fields['request']).fields['requestState'])
        self.assertTrue(self.manage(h).multi['request'][0].endswith('|spectator'))
    def test_waiting_polls_do_not_spend_the_small_admission_allowance(self):
        a,h=self.running(); r=self.request(a)
        for _ in range(30): self.assertEqual(200,self.status(r.fields["request"]).status)
    def test_abort_before_approval_prevents_a_racing_approval(self):
        a,h=self.running(); r=self.request(a); request=self.manage(h).multi["request"][0].split("|")[0]
        self.assertEqual(200,self.manage(h,"abort",request).status)
        self.assertEqual(409,self.manage(h,"approve",request).status)
        self.assertEqual("declined",self.status(r.fields["request"]).fields["requestState"])
    def test_disabled_legacy_room_does_not_accept_requests(self):
        a,h=self.seat('Host',visibility='public'); self.phase(h.fields['session'],'match')
        self.assertEqual(409,self.request(a).status)
    def test_abort_removes_only_newcomer_and_preserves_running_roster(self):
        a,h=self.running(); r=self.request(a); request=self.manage(h).multi['request'][0].split('|')[0]
        self.manage(h,'approve',request); guest=self.session(self.status(r.fields['request']).fields['grant'],'Guest')
        self.assertEqual(200,guest.status); self.assertEqual(200,self.manage(h,'abort',request).status)
        self.assertEqual(200,self.service.request('POST','/v1/p2p/poll',dict(cursor=0),headers={'X-Dune-Session':h.fields['session']}).status)
        self.assertNotEqual(200,self.service.request('POST','/v1/p2p/poll',dict(cursor=0),headers={'X-Dune-Session':guest.fields['session']}).status)

    def test_spectator_request_is_role_bound_and_uses_no_controller_assignment(self):
        a,h=self.running(); r=self.request(a,spectate=1)
        queue=self.manage(h).multi['request'][0]
        self.assertTrue(queue.endswith('|spectator'))
        request=queue.split('|')[0]
        self.assertEqual(200,self.manage(h,'approve_spectator',request).status)
        approved=self.status(r.fields['request'])
        self.assertEqual('approved',approved.fields['requestState'])
        self.assertEqual(200,self.session(approved.fields['grant'],'Guest').status)
        self.assertNotIn('request',self.manage(h).multi)
        self.assertEqual(200,self.phase(h.fields['session'],'match').status)

    def test_rejected_player_can_be_synchronized_as_a_spectator(self):
        a,h=self.running(); r=self.request(a)
        request=self.manage(h).multi['request'][0].split('|')[0]
        self.manage(h,'decline',request)
        self.assertTrue(self.manage(h).multi['request'][0].endswith('|spectator'))
        self.assertEqual(200,self.manage(h,'approve_spectator',request).status)
        self.assertEqual(200,self.session(self.status(r.fields['request']).fields['grant'],'Guest').status)

    def test_full_two_controller_game_remains_visible_for_spectators(self):
        a,h=self.running(mode='coop',maxPeers=2)
        for name in ('First','Second'):
            r=self.request(a,name,spectate=1)
            self.assertEqual(200,r.status)
            request=self.manage(h).multi['request'][0].split('|')[0]
            self.assertEqual(200,self.manage(h,'approve_spectator',request).status)
            self.assertEqual(200,self.session(self.status(r.fields['request']).fields['grant'],name).status)
            self.phase(h.fields['session'],'match')
        page=self.service.request('POST','/v1/admission/list',claims(offset=0,allMods=1,details=1))
        self.assertIn('game',page.multi)

    def test_passive_spectator_keeps_match_epoch_roster_and_host_only_links(self):
        from test_signaling import make_sdp
        a,h=self.running(gameProtocol=8)
        before=self.poll(h.fields['session'])
        r=self.request(a,'Watcher',gameProtocol=8,spectate=1)
        request=self.manage(h).multi['request'][0].split('|')[0]
        self.assertEqual(200,self.manage(h,'approve_spectator',request).status)
        self.assertEqual('match',self.poll(h.fields['session']).fields['phase'])
        approved=self.status(r.fields['request'],gameProtocol=8)
        watcher=self.session(approved.fields['grant'],'Watcher',gameProtocol=8)
        self.assertEqual(200,watcher.status)
        self.assertEqual('1',watcher.fields['spectator'])
        self.assertEqual('match',watcher.fields['phase'])
        rows=self.poll(h.fields['session']).multi['peer']
        self.assertTrue(any(row.endswith('|1') for row in rows))
        # Explicitly confirm the unchanged controller roster; no watcher ACK is involved.
        phase=self.service.request('POST','/v1/p2p/phase',dict(phase='match',roster=h.fields['peer']),headers={'X-Dune-Session':h.fields['session']})
        self.assertEqual(200,phase.status)
        r2=self.request(a,'Watcher2',gameProtocol=8,spectate=1)
        request2=self.manage(h).multi['request'][0].split('|')[0]
        self.manage(h,'approve_spectator',request2)
        second=self.session(self.status(r2.fields['request'],gameProtocol=8).fields['grant'],'Watcher2',gameProtocol=8)
        self.assertEqual(200,second.status)
        peer_ids={row.split('|')[0] for row in self.poll(watcher.fields['session']).multi['peer']}
        self.assertEqual({h.fields['peer'],watcher.fields['peer']},peer_ids)
        self.assertEqual(403,self.signal(watcher.fields['session'],int(second.fields['peer']),'offer',make_sdp(":".join(["AA"]*32))).status)
        self.assertEqual('match',self.poll(h.fields['session']).fields['phase'])

    def spectator(self, a, h, name='Watcher'):
        r=self.request(a,name,gameProtocol=9,spectate=1)
        self.assertEqual(200,r.status)
        request=self.manage(h).multi['request'][0].split('|')[0]
        self.assertEqual(200,self.manage(h,'approve_spectator',request).status)
        approved=self.status(r.fields['request'],gameProtocol=9)
        watcher=self.session(approved.fields['grant'],name,gameProtocol=9)
        self.assertEqual(200,watcher.status)
        return watcher

    def test_spectator_can_request_cancel_and_be_declined_without_leaving(self):
        a,h=self.running(gameProtocol=9)
        watcher=self.spectator(a,h)
        before=self.poll(h.fields['session'])
        self.assertEqual('pending',self.manage(watcher,'request_play').fields['playRequest'])
        queue=self.manage(h).multi['request']
        self.assertEqual(1,len(queue))
        request,name,role=queue[0].split('|')
        self.assertEqual(('Watcher','player'),(bytes.fromhex(name).decode(),role))
        # Repeated clicks are idempotent and cannot create another queued identity.
        self.assertEqual('pending',self.manage(watcher,'request_play').fields['playRequest'])
        self.assertEqual(queue,self.manage(h).multi['request'])
        self.assertEqual(200,self.manage(h,'decline',request).status)
        self.assertEqual('declined',self.manage(watcher,'play_status').fields['playRequest'])
        after=self.poll(watcher.fields['session'])
        self.assertEqual('match',after.fields['phase'])
        self.assertTrue(any(row.startswith(watcher.fields['peer']+'|') and row.endswith('|1') for row in after.multi['peer']))
        self.assertNotIn('request',self.manage(h).multi)
        self.assertEqual('pending',self.manage(watcher,'request_play').fields['playRequest'])
        request=self.manage(h).multi['request'][0].split('|')[0]
        self.assertEqual('cancelled',self.manage(watcher,'cancel_play').fields['playRequest'])
        self.assertEqual(409,self.manage(h,'approve',request).status)
        self.assertEqual(403,self.manage(h,'request_play').status)
        self.assertEqual(403,self.manage(watcher,'approve',request).status)

    def test_promotion_retains_authenticated_peer_and_adds_controller_only_after_approval(self):
        a,h=self.running(gameProtocol=9,maxPeers=2)
        watcher=self.spectator(a,h)
        self.assertEqual('pending',self.manage(watcher,'request_play').fields['playRequest'])
        request=self.manage(h).multi['request'][0].split('|')[0]
        self.assertEqual(409,self.manage(h,'approve_spectator',request).status)
        self.assertEqual(200,self.manage(h,'approve',request).status)
        self.assertEqual('approved',self.manage(watcher,'play_status').fields['playRequest'])
        self.assertEqual(200,self.manage(h,'approve',request).status)
        poll=self.poll(watcher.fields['session'])
        self.assertEqual('lobby',poll.fields['phase'])
        self.assertTrue(any(row.startswith(watcher.fields['peer']+'|') and row.endswith('|0') for row in poll.multi['peer']))
        self.assertEqual(409,self.manage(watcher,'request_play').status)
        self.assertEqual(200,self.manage(h,'abort',request).status)
        self.assertEqual('match',self.poll(h.fields['session']).fields['phase'])
        self.assertNotEqual(200,self.poll(watcher.fields['session']).status)

    def test_old_protocol_hosts_keep_original_queue_and_decline(self):
        a,h=self.running(gameProtocol=6)
        r=self.request(a,gameProtocol=6)
        queue=self.manage(h).multi['request'][0]
        self.assertEqual(2,len(queue.split('|')))
        self.manage(h,'decline',queue.split('|')[0])
        self.assertEqual('declined',self.status(r.fields['request'],gameProtocol=6).fields['requestState'])

if __name__=='__main__': unittest.main()
