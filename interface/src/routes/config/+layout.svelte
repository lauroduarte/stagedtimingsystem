<script lang="ts">
	import Menu from './menu.svelte';
	import Statusbar from './statusbar.svelte';
	import { onMount } from 'svelte';
	import { user } from '$lib/stores/user';
	import type { userProfile } from '$lib/stores/user';
	import { socket } from '$lib/stores/socket';
	import { page } from '$app/state';
	import Login from './login.svelte';
	import { setUser } from '$lib/services/services';

	let { children } = $props();

	let menuOpen = $state(false);

	setUser($user);

	$effect(() => {
		setUser($user);
	});

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
		setUser($user);
		const ws_token = page.data.features.security ? '?access_token=' + $user.bearer_token : '';
		socket.init(
			`ws://${window.location.host}/ws/events${ws_token}`,
			page.data.features.event_use_json
		);
	};

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
	<div class="drawer lg:drawer-open min-h-screen">
		<input id="main-menu" type="checkbox" class="drawer-toggle" bind:checked={menuOpen} />
		<div class="drawer-content flex flex-col min-w-0 w-full overflow-x-hidden">
			<!-- Status bar content here -->
			<Statusbar />

			<!-- Main page content here -->
			<main class="flex-1 w-full min-w-0 p-2 sm:p-4 md:p-6">
				{@render children?.()}
			</main>
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
