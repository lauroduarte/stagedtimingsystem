<script lang="ts">
	import Flag from '~icons/tabler/flag';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onMount } from 'svelte';

	import type { RFIDCard } from '$lib/types/models';
	import { startRFIDReset, stopRFIDReset } from '$lib/services/services';

</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Iniciar Reset dos Cards RFID</h1>

		<div class="bg-white p-6 rounded shadow max-w-xl">

			<button
				on:click={startRFIDReset}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Iniciar Reset
			</button>

			<button
				on:click={stop}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Parar Reset
			</button>

		</div>
	</div>
</SettingsCard>
