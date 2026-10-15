"""Exercise the migrated gateway without a live Music Assistant or device."""
import asyncio
import json
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from aiohttp import WSMsgType, web, WSServerHandshakeError
from aiohttp.test_utils import TestClient, TestServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import app as gateway


class ValidationTests(unittest.TestCase):
    def test_valid_commands(self):
        for command, args in [
            ('players/all', {}), ('providers', {}),
            ('players/cmd/volume_set', {'player_id': 'speaker', 'volume_level': 100}),
            ('player_queues/items', {'queue_id': 'speaker', 'limit': 50, 'offset': 0}),
            ('music/album_tracks', {'item_id': 'album', 'provider_instance_id_or_domain': 'library'}),
            ('player_queues/play_media', {'queue_id': 'speaker', 'media': {'uri': 'library://track/1'}}),
        ]:
            with self.subTest(command=command):
                self.assertEqual(gateway.validate_command(command, args), args)

    def test_rejects_admin_commands_types_and_bounds(self):
        for command, args in [
            ('auth/login', {}), ('config/set', {}), ('providers', {'extra': 1}),
            ('players/cmd/play', {}), ('players/cmd/play', {'player_id': ''}),
            ('players/cmd/volume_set', {'player_id': 'speaker', 'volume_level': True}),
            ('players/cmd/volume_set', {'player_id': 'speaker', 'volume_level': 101}),
            ('music/albums', {'limit': 0}), ('music/albums', {'limit': 101}),
            ('music/albums', {'offset': -1}), ('players/all', []),
            ('player_queues/play_media', {'queue_id': 'speaker', 'media': {'uri': 'x' * 2049}}),
        ]:
            with self.subTest(command=command, args=args):
                with self.assertRaises(ValueError):
                    gateway.validate_command(command, args)


class RelayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.received = []
        self.reject_login = False
        self.upstream_app = web.Application()
        self.upstream_app.router.add_get('/ws', self.fake_ma)
        self.upstream = TestServer(self.upstream_app)
        await self.upstream.start_server()
        self.config = patch.multiple(
            gateway, MA_URL=str(self.upstream.make_url('/ws')),
            USERNAME='test-controller', PASSWORD='test-password',
            DEVICE_TOKEN='test-device-token', CONTROLLER_ORIGIN='https://controller.example.net',
        )
        self.config.start()
        self.client = TestClient(TestServer(gateway.create_app()))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        await self.upstream.close()
        self.config.stop()

    async def fake_ma(self, request):
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        await ws.send_json({'server_info': 'not forwarded'})
        async for message in ws:
            if message.type != WSMsgType.TEXT:
                continue
            data = json.loads(message.data)
            self.received.append(data)
            name = data['command']
            if name == 'auth/login':
                result = {'success': not self.reject_login, 'access_token': 'private-upstream-token'}
            elif name == 'auth':
                result = {'success': True}
            else:
                result = [{'player_id': 'speaker'}]
            await ws.send_json({'message_id': data['message_id'], 'result': result})
            if name == 'auth':
                await ws.send_json({'event': 'player_updated', 'data': {'player_id': 'speaker'}})
        return ws

    async def test_rejects_missing_wrong_token_and_origin_before_upstream_login(self):
        for path, headers in [
            ('/device/ws', {}), ('/device/ws', {'X-Device-Token': 'wrong'}),
            ('/ws', {}), ('/ws', {'Origin': 'https://other.example.net'}),
        ]:
            with self.subTest(path=path, headers=headers):
                with self.assertRaises(WSServerHandshakeError) as error:
                    await self.client.ws_connect(path, headers=headers)
                self.assertEqual(error.exception.status, 403)
        self.assertEqual(self.received, [])

    async def test_empty_credentials_fail_closed(self):
        with patch.object(gateway, 'DEVICE_TOKEN', ''):
            with self.assertRaises(WSServerHandshakeError):
                await self.client.ws_connect('/device/ws', headers={'X-Device-Token': ''})
        with patch.object(gateway, 'CONTROLLER_ORIGIN', ''):
            with self.assertRaises(WSServerHandshakeError):
                await self.client.ws_connect('/ws')

    async def exercise_relay(self, path, headers):
        ws = await self.client.ws_connect(path, headers=headers)
        self.assertEqual(await ws.receive_json(timeout=2), {'event': 'gateway/ready'})
        capabilities = await ws.receive_json(timeout=2)
        self.assertEqual(capabilities, {'event': 'gateway/capabilities', 'data': gateway.DEVICE_CAPABILITIES})
        self.assertEqual((await ws.receive_json(timeout=2))['event'], 'player_updated')
        self.assertEqual([x['command'] for x in self.received], ['auth/login', 'auth'])
        self.assertEqual(self.received[0]['args']['password'], 'test-password')
        for payload in ['{', '[]', 'null', '{"command":"auth/login","message_id":"bad"}',
                        '{"command":"players/all","message_id":"bad","args":{"extra":true}}']:
            await ws.send_str(payload)
            response = await ws.receive_json(timeout=2)
            self.assertIn('error', response)
        self.assertEqual(len(self.received), 2)
        await ws.send_json({'message_id': 7, 'command': 'players/all', 'args': {}})
        self.assertEqual(await ws.receive_json(timeout=2), {
            'message_id': '7', 'result': [{'player_id': 'speaker'}],
        })
        await ws.close()

    async def test_device_authentication_validation_and_relay(self):
        await self.exercise_relay('/device/ws', {'X-Device-Token': 'test-device-token'})

    async def test_browser_origin_and_relay(self):
        await self.exercise_relay('/ws', {'Origin': 'https://controller.example.net'})

    async def test_upstream_login_failure_does_not_send_ready(self):
        self.reject_login = True
        ws = await self.client.ws_connect('/device/ws', headers={'X-Device-Token': 'test-device-token'})
        self.assertEqual((await ws.receive_json(timeout=2))['event'], 'gateway/error')
        self.assertEqual([x['command'] for x in self.received], ['auth/login'])
        await ws.close()

    async def test_application_factory_exposes_authenticated_artwork(self):
        # PR #20 derives this sibling HTTP endpoint from its provisioned WSS URL.
        response = await self.client.get('/device/artwork/generic')
        self.assertEqual(response.status, 403)
        response = await self.client.get('/device/artwork/generic',
                                         headers={'X-Device-Token': 'test-device-token'})
        self.assertEqual(response.status, 200)
        self.assertEqual(response.headers['X-Artwork-Fallback'], '1')
        packet = await response.read()
        self.assertEqual(len(packet), 8 + 288 * 288 * 2)
        self.assertEqual(packet[:8], b'MCAR\x20\x01\x20\x01')
        self.assertEqual(self.received, [])

    async def test_health_is_configuration_check_not_upstream_probe(self):
        response = await self.client.get('/health')
        self.assertEqual(response.status, 200)
        self.assertEqual(self.received, [])
        with patch.object(gateway, 'PASSWORD', ''):
            response = await self.client.get('/health')
            self.assertEqual(response.status, 503)

    async def test_device_settings_are_local_and_acknowledged(self):
        ws = await self.client.ws_connect('/device/ws', headers={'X-Device-Token':'test-device-token'})
        self.assertEqual((await ws.receive_json())['event'], 'gateway/ready')
        await ws.receive_json()  # capabilities
        state = dict(device_id='001122aabbcc', battery_seconds=30, usb_seconds=0,
                     battery_default=30, usb_default=0, power_mode=0, power_source='unknown', saved=True)
        await ws.send_json({'event':'controller/settings/state','data':state})
        # An ordered MA request/reply confirms the state message was consumed first.
        await ws.send_json({'message_id':'sync','command':'players/all','args':{}})
        while (await ws.receive_json()).get('message_id') != 'sync':
            pass
        snapshot = await (await self.client.get('/settings/api')).json()
        self.assertEqual(snapshot[state['device_id']]['battery_seconds'],30)
        change = dict(battery_seconds=60,usb_seconds=0,power_mode=0)
        pending = asyncio.create_task(self.client.put('/settings/api/'+state['device_id'],
            json=change,headers={'Origin':'https://controller.example.net'}))
        command = await ws.receive_json(timeout=2)
        self.assertEqual(command['event'],'controller/settings/set')
        self.assertFalse(pending.done())
        await ws.send_json({'event':'controller/settings/state','data':{
            **state,**change,'request_id':command['data']['request_id']}})
        response = await pending
        self.assertEqual(response.status,200)
        self.assertEqual((await response.json())['battery_seconds'],60)
        self.assertFalse(any(x.get('command','').startswith('controller/settings') for x in self.received))
        await ws.close()
