import asyncio
import sys
from pathlib import Path
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from settings import DeviceSettings

STATE = dict(device_id='001122aabbcc', battery_seconds=30, usb_seconds=0,
             battery_default=30, usb_default=0, power_mode=0, power_source='unknown', saved=True)


def test_settings_device_ack_offline_and_validation():
    async def scenario():
        store = DeviceSettings('https://controller.test')
        app = web.Application(); store.routes(app)
        class Device:
            closed = False
            sent = None
            async def send_json(self, message):
                self.sent = message
        device = Device()
        async with TestClient(TestServer(app)) as client:
            assert (await client.get('/settings')).status == 200
            assert await (await client.get('/settings/api')).json() == {}
            data = dict(battery_seconds=60,usb_seconds=300,power_mode=0)
            path = '/settings/api/'+STATE['device_id']
            headers = {'Origin':'https://controller.test'}
            assert (await client.put(path,json=data)).status == 403
            assert (await client.put(path,json=data,headers={'Origin':'https://evil.test'})).status == 403
            assert (await client.put(path,json=data,headers=headers)).status == 409
            await store.receive(device, STATE)
            assert (await client.put(path,json={**data,'usb_seconds':True},headers=headers)).status == 400
            assert (await client.put(path,json={**data,'battery_seconds':-1},headers=headers)).status == 400
            request = asyncio.create_task(client.put(path,json=data,headers=headers))
            while not device.sent: await asyncio.sleep(0)
            assert not request.done()  # Never report success before durable device acknowledgement.
            assert (await client.put(path,json=data,headers=headers)).status == 409
            request_id = device.sent['data']['request_id']
            await store.receive(device,{**STATE,**data,'request_id':request_id})
            response=await request
            assert response.status == 200
            assert (await response.json())['battery_seconds'] == 60
            device.sent=None
            request=asyncio.create_task(client.put(path,json=data,headers=headers))
            while not device.sent: await asyncio.sleep(0)
            store.disconnect(device)
            assert (await request).status == 504
            assert await (await client.get('/settings/api')).json() == {}
    asyncio.run(scenario())
