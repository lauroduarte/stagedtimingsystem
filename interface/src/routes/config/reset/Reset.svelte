<script lang="ts">
	import Flag from '~icons/tabler/flag';
	import SettingsCard from '$lib/components/SettingsCard.svelte';
	import { page } from '$app/state';
	import { notifications } from '$lib/components/toasts/notifications';
	import { user } from '$lib/stores/user';
	import { on } from 'svelte/events';
	import { onDestroy, onMount } from 'svelte';

	import type { RFIDCard } from '$lib/types/models';
	import { getRFIDCards, startRFIDReset, stopRFIDReset } from '$lib/services/services';
	import { get } from 'svelte/store';
	import { socket } from '$lib/stores/socket';


	let rfidCards: RFIDCard[] = $state([]);

	onMount(async () => {
		// Carrega as RFIDs ao montar o componente
		try {
			const rfidData = await getRFIDCards();
			// Verifica se rfidData é um array e atualiza rfidCards
			if (Array.isArray(rfidData)) {
				rfidCards.splice(0, rfidCards.length, ...rfidData);
			}
		} catch (error) {
			notifications.error('Erro ao carregar os RFIDs. Tente novamente.', 3000);
		}

		socket.on<RFIDCard[]>('rfid', (data) => {
			// Verifica se data é um array antes de atualizar rfidCards
			if (Array.isArray(data)) {
				rfidCards.splice(0, rfidCards.length, ...data);
			}
			notifications.success('RFID atualizado com sucesso!', 3000);
		});
	});

	onDestroy(() => {
		// Limpa o socket ao destruir o componente
		socket.off('rfid');
	});
</script>

<SettingsCard collapsible={false}>
	<div class="space-y-6">
		<h1 class="text-2xl font-bold">Iniciar Reset dos Cards RFID</h1>

		<div class="bg-white p-6 rounded shadow max-w-xl">
			{#if rfidCards.length > 0}
				<table class="min-w-full">
					<thead>
						<tr>
							<th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">UID</th>
							<th class="px-6 py-3 text-left text-xs font-medium text-gray-500 uppercase tracking-wider">Número</th>
						</tr>
					{#each rfidCards as card}
						<tr>
							<td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900">{card.uid}</td>
							<td class="px-6 py-4 whitespace-nowrap text-sm text-gray-900">{card.numero}</td>
						</tr>
					{/each}
				</table>
			{:else}
				<p class="text-gray-500">Nenhum card RFID encontrado.</p>
			{/if}

			<button
				on:click={startRFIDReset}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Iniciar Reset
			</button>

			<button
				on:click={stopRFIDReset}
				class="bg-blue-600 text-white px-4 py-2 rounded hover:bg-blue-700 transition"
			>
				Parar Reset
			</button>

		</div>
	</div>
</SettingsCard>
