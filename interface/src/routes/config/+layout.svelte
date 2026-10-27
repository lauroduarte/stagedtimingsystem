<script lang="ts">
	import Menu from './menu.svelte';
	import Statusbar from './statusbar.svelte';
	import { onDestroy, onMount } from 'svelte';
	import { user } from '$lib/stores/user';
	import type { userProfile } from '$lib/stores/user';
	import { socket } from '$lib/stores/socket';
	import { page } from '$app/state';
    import Login from './login.svelte';	
	import { setUser } from '$lib/services/services';
	import { notifications } from '$lib/components/toasts/notifications';
	import type { Analytics } from '$lib/types/models';
	import type { RSSI } from '$lib/types/models';
	import type { Battery } from '$lib/types/models';
	import type { DownloadOTA } from '$lib/types/models';
	import { telemetry } from '$lib/stores/telemetry';
	import { analytics } from '$lib/stores/analytics';
	import { batteryHistory } from '$lib/stores/battery';		

	let { children } = $props();

	let menuOpen = $state(false);

	onMount(async () => {
		if ($user.bearer_token !== '') {
			await validateUser($user);
		}
		if (!(page.data.features.security && $user.bearer_token === '')) {
			setUser($user);
			initSocket();
		}
	});

	const initSocket = () => {
		const ws_token = page.data.features.security ? '?access_token=' + $user.bearer_token : '';
		socket.init(
			`ws://${window.location.host}/ws/events${ws_token}`,
			page.data.features.event_use_json
		);
		addEventListeners();
	};	

	const addEventListeners = () => {
		socket.on('open', handleOpen);
		socket.on('close', handleClose);
		socket.on('error', handleError);
		socket.on('rssi', handleNetworkStatus);
		socket.on('notification', handleNotification);
		if (page.data.features.analytics) socket.on('analytics', handleAnalytics);
		if (page.data.features.battery) socket.on('battery', handleBattery);
		if (page.data.features.download_firmware) socket.on('otastatus', handleOAT);
	};	

	const removeEventListeners = () => {
		socket.off('analytics', handleAnalytics);
		socket.off('open', handleOpen);
		socket.off('close', handleClose);
		socket.off('rssi', handleNetworkStatus);
		socket.off('notification', handleNotification);
		socket.off('battery', handleBattery);
		socket.off('otastatus', handleOAT);
	};
	
	onDestroy(() => {
		removeEventListeners();
	});

	const handleOpen = () => {
		notifications.success('Connection to device established', 5000);
	};

	const handleClose = () => {
		notifications.error('Connection to device lost', 5000);
		telemetry.setRSSI({ rssi: 0, ssid: '' });
	};

	const handleError = (data: any) => console.error(data);

	const handleNotification = (data: any) => {
		switch (data.type) {
			case 'info':
				notifications.info(data.message, 5000);
				break;
			case 'warning':
				notifications.warning(data.message, 5000);
				break;
			case 'error':
				notifications.error(data.message, 5000);
				break;
			case 'success':
				notifications.success(data.message, 5000);
				break;
			default:
				break;
		}
	};

	const handleAnalytics = (data: Analytics) => analytics.addData(data);

	const handleNetworkStatus = (data: RSSI) => telemetry.setRSSI(data);

	const handleBattery = (data: Battery) => {
		telemetry.setBattery(data);
		batteryHistory.addData(data);
	};

	const handleOAT = (data: DownloadOTA) => telemetry.setDownloadOTA(data);		


	async function validateUser(userdata: userProfile) {
		try {
			const response = await fetch('/rest/verifyAuthorization', {
				method: 'GET',
				headers: {
					Authorization: 'Bearer ' + userdata.bearer_token,
					'Content-Type': 'application/json'
				}
			});
			if (response.status !== 200) {
				user.invalidate();
			}
		} catch (error) {
			console.error('Error:', error);
		}
	}	

</script>

{#if page.data.features.security && $user.bearer_token === ''}
	<Login signIn={initSocket} />
{:else}
	<div class="drawer lg:drawer-open">
		<input id="main-menu" type="checkbox" class="drawer-toggle" bind:checked={menuOpen} />
		<div class="drawer-content flex flex-col">
			<!-- Status bar content here -->
			<Statusbar />

			<!-- Main page content here -->
			{@render children?.()}
		</div>
		<!-- Side Navigation -->
		<div class="drawer-side z-30 shadow-lg">
			<label for="main-menu" class="drawer-overlay"></label>
			<Menu
				closeMenu={() => {
					menuOpen = false;
				}}
			/>
		</div>
	</div>
{/if}
